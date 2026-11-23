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
    check(core.step(ram) == machine::RunResult::FetchFault,
          "core refuses instruction fetch from non-executable page");

    // XEX launch authorization is covered by xex_synthetic_boot_tests,
    // which obtains the non-forgeable token from XexImageLoader. This unit
    // test intentionally exercises CPU/memory behavior only.

    core.state.pc = 0x5000;
    check(core.step(ram) == machine::RunResult::FetchFault,
          "core reports fetch outside physical RAM");

    core.state.pc = 0x102;
    check(core.step(ram) == machine::RunResult::InstructionAlignmentFault,
          "core distinguishes misaligned instruction fetch");
    if (failures == 0) std::cout << "WX360 Xenon core integration tests passed\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
