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
    if (failures == 0) std::cout << "WX360 Xenon core integration tests passed\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
