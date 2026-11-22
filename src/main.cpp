#include "wx360/cpu/powerpc_interpreter.hpp"
#include "wx360/ui/aurora_window.hpp"
#if defined(_WIN32)
#include <windows.h>
#endif

#include <iomanip>
#include <iostream>
#include <string_view>

namespace {
constexpr std::string_view kVersion = "0.3b";
bool has_sse2() noexcept {
#if defined(_M_X64) || defined(__x86_64__)
    return true;
#elif defined(_M_IX86) && defined(_MSC_VER)
    int cpu_info[4]{};
    __cpuid(cpu_info, 1);
    return (cpu_info[3] & (1 << 26)) != 0;
#elif defined(__i386__) && (defined(__GNUC__) || defined(__clang__))
    unsigned int eax = 1, ebx = 0, ecx = 0, edx = 0;
    __asm__ volatile("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    return (edx & (1u << 26)) != 0;
#else
    return false;
#endif
}
}

int main() {
    std::cout << "WX360 - Windows Xbox 360 Emulator\n"
              << "Version: " << kVersion << "\n"
              << "CPU baseline: x86-64 / SSE2; AVX2 is not required.\n";
    if (!has_sse2()) {
        std::cerr << "This build requires an SSE2-capable CPU.\n";
        return 1;
    }
    wx360::cpu::XenonThreadState thread;
    thread.reset(0x10000);
    const auto result = wx360::cpu::execute(thread, 0x38600001U); // addi r3,r0,1
    std::cout << "Interpreter smoke test: "
              << (result == wx360::cpu::StepResult::Executed ? "instruction executed" : "unsupported")
              << "; r3=" << thread.gpr[3] << "; PC=0x"
              << std::hex << thread.pc << std::dec << "\n"
              << "Status: limited PowerPC interpreter; not a complete Xenon CPU.\n";
    if (result != wx360::cpu::StepResult::Executed || thread.gpr[3] != 1) return 2;
#if defined(_WIN32)
    wx360::ui::AuroraWindow dashboard;
    return dashboard.run(GetModuleHandleW(nullptr), SW_SHOW);
#else
    return 0;
#endif
}
