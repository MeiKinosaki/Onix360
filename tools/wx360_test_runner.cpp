#include "wx360/cpu/powerpc_interpreter.hpp"
#include "wx360/kernel/xex_import_resolver.hpp"
#include "wx360/machine/xenon_core.hpp"
#include "wx360/memory/physical_memory.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <string_view>

namespace {
int failures = 0;
void check(bool condition, std::string_view name) {
    std::cout << (condition ? "[PASS] " : "[FAIL] ") << name << '\n';
    if (!condition) ++failures;
}
}

int main() {
    using namespace wx360;
    std::cout << "WX360 Emulator Diagnostic Runner\n"
              << "Synthetic hardware/software tests only; no commercial XEX is launched.\n\n";

    cpu::XenonThreadState thread;
    thread.reset(0x10000);
    const auto instruction = cpu::execute(thread, 0x3860002AU); // addi r3,r0,42
    check(instruction == cpu::StepResult::Executed && thread.gpr[3] == 42 &&
          thread.pc == 0x10004, "Xenon interpreter ADDI smoke test");

    memory::PhysicalMemory ram(0x4000);
    const std::array<std::uint8_t,4> addi{0x38,0x60,0x00,0x2A};
    check(ram.write(0x100, addi) == memory::MemoryResult::Ok,
          "guest RAM write");
    machine::XenonCore core;
    core.state.reset(0x100);
    check(core.step(ram) == machine::RunResult::Executed &&
          core.state.gpr[3] == 42, "Xenon core fetch/decode/execute");

    kernel::XexImportResolver resolver;
    const bool function_added = resolver.register_export(
        {"xboxkrnl.exe", 0x123, 0x220, kernel::ImportKind::Function});
    const bool variable_added = resolver.register_export(
        {"xboxkrnl.exe", 0x124, 0x82002000, kernel::ImportKind::Variable});
    const std::array imports{
        kernel::XexImport{"xboxkrnl.exe",0x123,0x200,kernel::ImportKind::Function},
        kernel::XexImport{"xboxkrnl.exe",0x124,0x204,kernel::ImportKind::Variable}
    };
    const auto resolved = resolver.resolve(imports, ram);
    const auto bytes = ram.raw_bytes();
    const auto be = [&](std::size_t at, std::uint32_t v) {
        return bytes[at] == static_cast<std::uint8_t>(v >> 24) &&
               bytes[at+1] == static_cast<std::uint8_t>(v >> 16) &&
               bytes[at+2] == static_cast<std::uint8_t>(v >> 8) &&
               bytes[at+3] == static_cast<std::uint8_t>(v);
    };
    check(function_added && variable_added && resolved == kernel::ImportResolveError::None &&
          be(0x200,0x220) && be(0x204,0x3000),
          "typed kernel function/variable import resolution");

    const std::array invalid{
        kernel::XexImport{"xboxkrnl.exe",0x124,0x228,kernel::ImportKind::Function,0x208}
    };
    check(resolver.resolve(invalid,ram) == kernel::ImportResolveError::KindMismatch &&
          bytes[0x208] == 0, "reject mismatched import type without patching");

    std::cout << "\nResult: " << (failures == 0 ? "ALL DIAGNOSTICS PASSED" : "FAILURES DETECTED")
              << " (" << failures << " failure(s))\n";
    return failures == 0 ? 0 : 1;
}
