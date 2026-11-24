#include "wx360/machine/xenon_core.hpp"

#include <array>
#include <cstdlib>
#include <iostream>

namespace {
int failures = 0;
void check(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
}

int main() {
    using namespace wx360;
    memory::PhysicalMemory ram(0x4000);
    const std::array<std::uint8_t, 4> addi{0x38, 0x60, 0x00, 0x2A};
    check(ram.write(0x100, addi) == memory::MemoryResult::Ok,
          "place ADDI instruction in guest RAM");
    machine::XenonCore core;
    core.state.reset(0x100);
    check(core.step(ram) == machine::RunResult::Executed,
          "core fetches and executes instruction from guest RAM");
    check(core.state.gpr[3] == 42 && core.state.pc == 0x104,
          "core updates GPR and PC after instruction execution");

    check(ram.set_page_permissions(0x100, memory::PhysicalMemory::page_size,
          static_cast<std::uint8_t>(memory::Access::Read)) == memory::MemoryResult::Ok,
          "remove execute permission from code page");
    core.state.pc = 0x100;
    check(core.step(ram) == machine::RunResult::ExceptionTaken,
          "core raises instruction-storage exception for non-executable page");
    check(core.state.pc == 0x400 && core.state.srr0 == 0x100 &&
          core.state.exception_pending && core.state.exception_vector == 0x400,
          "instruction-storage exception saves PC and selects vector 0x400");

    // XEX launch authorization is covered by xex_synthetic_boot_tests,
    // which obtains the non-forgeable token from XexImageLoader. This unit
    // test intentionally exercises CPU/memory behavior only.

    core.state.pc = 0x5000;
    check(core.step(ram) == machine::RunResult::ExceptionTaken,
          "core raises instruction-storage exception outside physical RAM");
    check(core.state.pc == 0x400 && core.state.srr0 == 0x5000,
          "out-of-range instruction fetch records faulting PC");

    core.state.pc = 0x102;
    check(core.step(ram) == machine::RunResult::ExceptionTaken,
          "core raises alignment exception for misaligned instruction fetch");
    check(core.state.pc == 0x600 && core.state.srr0 == 0x102 &&
          core.state.dar == 0x102 && core.state.exception_vector == 0x600,
          "alignment exception saves PC, fault address and vector");
    memory::PhysicalMemory data_ram(0x4000);
    const std::array<std::uint8_t, 4> lwz_r4_r3{0x80, 0x83, 0x00, 0x00};
    check(data_ram.write(0x200, lwz_r4_r3) == memory::MemoryResult::Ok,
          "place LWZ instruction for data-alignment exception test");
    machine::XenonCore data_core;
    data_core.state.reset(0x200);
    data_core.state.gpr[3] = 0x101;
    check(data_core.step(data_ram) == machine::RunResult::ExceptionTaken,
          "misaligned LWZ enters alignment exception");
    check(data_core.state.pc == 0x600 && data_core.state.srr0 == 0x200 &&
          data_core.state.dar == 0x101,
          "data alignment exception saves effective address in DAR");

    // Exercise guest virtual instruction fetch through the page translator.
    memory::PhysicalMemory virtual_ram(0x4000);
    check(virtual_ram.write(0x100, addi) == memory::MemoryResult::Ok,
          "place instruction at translated physical address");
    machine::XenonCore virtual_core;
    virtual_core.state.reset(0x1100);
    check(virtual_core.map_guest_page(0x1000, 0x0000,
          static_cast<std::uint8_t>(memory::Access::Read) |
          static_cast<std::uint8_t>(memory::Access::Write) |
          static_cast<std::uint8_t>(memory::Access::Execute)) == memory::MemoryResult::Ok,
          "map guest virtual page to physical page");
    check(virtual_core.step(virtual_ram) == machine::RunResult::Executed,
          "fetches instruction through virtual address translation");
    check(virtual_core.state.gpr[3] == 42 && virtual_core.state.pc == 0x1104,
          "translated fetch preserves virtual PC progression");

    // D-form data instructions must translate their effective virtual address,
    // not index the physical RAM span directly.
    const std::array<std::uint8_t, 4> lwz_virtual{0x80, 0x83, 0x00, 0x00};
    const std::array<std::uint8_t, 4> stw_virtual{0x90, 0x83, 0x00, 0x04};
    const std::array<std::uint8_t, 4> physical_value{0xDE, 0xAD, 0xBE, 0xEF};
    check(virtual_ram.write(0x104, lwz_virtual) == memory::MemoryResult::Ok &&
          virtual_ram.write(0x108, stw_virtual) == memory::MemoryResult::Ok,
          "place virtual LWZ and STW instructions in translated code page");
    check(virtual_ram.write(0x2000, physical_value) == memory::MemoryResult::Ok,
          "initialize data at translated physical page");
    check(virtual_core.map_guest_page(0x3000, 0x2000,
          static_cast<std::uint8_t>(memory::Access::Read) |
          static_cast<std::uint8_t>(memory::Access::Write)) == memory::MemoryResult::Ok,
          "map guest data page to a different physical page");
    virtual_core.state.pc = 0x1104;
    virtual_core.state.gpr[3] = 0x3000;
    check(virtual_core.step(virtual_ram) == machine::RunResult::Executed &&
          virtual_core.state.gpr[4] == 0xDEADBEEFU,
          "LWZ reads through the guest virtual-to-physical mapping");
    virtual_core.state.pc = 0x1108;
    virtual_core.state.gpr[4] = 0x12345678U;
    check(virtual_core.step(virtual_ram) == machine::RunResult::Executed,
          "STW writes through the guest virtual-to-physical mapping");
    std::array<std::uint8_t, 4> stored_value{};
    check(virtual_ram.read(0x2004, stored_value) == memory::MemoryResult::Ok &&
          stored_value == std::array<std::uint8_t, 4>{0x12, 0x34, 0x56, 0x78},
          "STW updates translated physical memory in big-endian order");

    check(virtual_core.map_guest_page(0x4000, 0x2000,
          static_cast<std::uint8_t>(memory::Access::Read)) == memory::MemoryResult::Ok,
          "map a read-only guest data page");
    const std::array<std::uint8_t, 4> stw_read_only{0x90, 0x83, 0x00, 0x00};
    check(virtual_ram.write(0x10C, stw_read_only) == memory::MemoryResult::Ok,
          "place STW instruction for guest write-protection test");
    virtual_core.state.pc = 0x110C;
    virtual_core.state.gpr[3] = 0x4000;
    check(virtual_core.step(virtual_ram) == machine::RunResult::ExceptionTaken &&
          virtual_core.state.exception_vector == 0x300,
          "STW to read-only virtual page raises data-storage exception");
    check(virtual_core.state.dar == 0x4000,
          "data-storage protection exception records faulting guest address in DAR");
    const std::array<std::uint8_t, 4> lwz_unmapped{0x80, 0x83, 0x00, 0x00};
    check(virtual_ram.write(0x118, lwz_unmapped) == memory::MemoryResult::Ok,
          "place LWZ for unmapped guest data test");
    virtual_core.state.pc = 0x1118;
    virtual_core.state.gpr[3] = 0x6000;
    check(virtual_core.step(virtual_ram) == machine::RunResult::ExceptionTaken &&
          virtual_core.state.exception_vector == 0x300 &&
          virtual_core.state.dar == 0x6000,
          "unmapped guest data access raises DSI and records DAR");
    virtual_core.state.pc = 0x9000;
    check(virtual_core.step(virtual_ram) == machine::RunResult::ExceptionTaken &&
          virtual_core.state.exception_vector == 0x400 &&
          virtual_core.state.dar == 0x9000,
          "unmapped guest instruction fetch raises ISI and records faulting PC");

    // Indexed LWZX/STWX use the same translated guest memory bus.
    const auto xform = [](unsigned rt, unsigned ra, unsigned rb, unsigned xo) {
        return (31U << 26U) | (rt << 21U) | (ra << 16U) | (rb << 11U) | (xo << 1U);
    };
    const auto encode_be = [](std::uint32_t word) {
        return std::array<std::uint8_t, 4>{
            static_cast<std::uint8_t>(word >> 24U),
            static_cast<std::uint8_t>(word >> 16U),
            static_cast<std::uint8_t>(word >> 8U),
            static_cast<std::uint8_t>(word)};
    };
    const auto lwzx = encode_be(xform(4, 3, 5, 23));
    const auto stwx = encode_be(xform(4, 3, 5, 151));
    check(virtual_ram.write(0x110, lwzx) == memory::MemoryResult::Ok &&
          virtual_ram.write(0x114, stwx) == memory::MemoryResult::Ok,
          "place indexed LWZX and STWX instructions");
    check(virtual_core.map_guest_page(0x5000, 0x2000,
          static_cast<std::uint8_t>(memory::Access::Read) |
          static_cast<std::uint8_t>(memory::Access::Write)) == memory::MemoryResult::Ok,
          "map indexed data virtual page");
    virtual_core.state.gpr[3] = 0x5000;
    virtual_core.state.gpr[5] = 0;
    virtual_core.state.pc = 0x1110;
    check(virtual_core.step(virtual_ram) == machine::RunResult::Executed &&
          virtual_core.state.gpr[4] == 0xDEADBEEFU,
          "LWZX loads from translated indexed virtual address");
    virtual_core.state.gpr[4] = 0xCAFEBABEU;
    virtual_core.state.pc = 0x1114;
    check(virtual_core.step(virtual_ram) == machine::RunResult::Executed,
          "STWX stores to translated indexed virtual address");
    check(virtual_ram.read(0x2000, stored_value) == memory::MemoryResult::Ok &&
          stored_value == std::array<std::uint8_t, 4>{0xCA, 0xFE, 0xBA, 0xBE},
          "STWX updates translated physical memory");

    const std::array<std::uint8_t, 4> guest_data{0x11, 0x22, 0x33, 0x44};
    check(virtual_core.write_guest(virtual_ram, 0x1120, guest_data) == memory::MemoryResult::Ok,
          "writes data through guest virtual address");
    std::array<std::uint8_t, 4> guest_read{};
    check(virtual_core.read_guest(virtual_ram, 0x1120, guest_read) == memory::MemoryResult::Ok &&
          guest_read == guest_data, "reads data through guest virtual address");
    check(virtual_core.write_guest(virtual_ram, 0x1120, guest_data) ==
          memory::MemoryResult::Ok, "guest write translation remains available");
    check(virtual_core.read_guest(virtual_ram, 0x2120, guest_read) ==
          memory::MemoryResult::Unmapped, "guest data access faults on unmapped page");

    if (failures == 0) std::cout << "WX360 Xenon core integration tests passed\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
