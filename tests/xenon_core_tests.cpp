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

    xex::LaunchPlan pending_plan{};
    pending_plan.entry_point = 0x100;
    check(core.start_xex(pending_plan, ram) == machine::LaunchResult::ImageNotMapped,
          "XEX handoff refuses an image that has not been mapped");

    pending_plan.decryption_complete = true;
    pending_plan.decompression_complete = true;
    pending_plan.pe_mapped = true;
    pending_plan.kernel_imports_resolved = true;
    pending_plan.mapped_entry_point = 0x100;
    check(core.start_xex(pending_plan, ram) == machine::LaunchResult::EntryPointNotExecutable,
          "XEX handoff rejects a mapped entry point without execute permission");

    check(ram.set_page_permissions(0x100, memory::PhysicalMemory::page_size,
          static_cast<std::uint8_t>(memory::Access::Read) |
          static_cast<std::uint8_t>(memory::Access::Execute)) == memory::MemoryResult::Ok,
          "restore execute permission for mapped image");
    check(core.start_xex(pending_plan, ram) == machine::LaunchResult::Started &&
          core.state.pc == 0x100,
          "Xenon core accepts an executable mapped XEX entry point");

    core.state.pc = 0x5000;
    check(core.step(ram) == machine::RunResult::FetchFault,
          "core reports fetch outside physical RAM");
    if (failures == 0) std::cout << "WX360 Xenon core integration tests passed\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
