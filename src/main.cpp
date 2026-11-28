#include "onix360/cpu/powerpc_interpreter.hpp"
#include "onix360/ui/aurora_window.hpp"
#include "onix360/storage/data_directories.hpp"
#if defined(_WIN32)
#include <windows.h>
#endif

#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string_view>

namespace {
constexpr std::string_view kVersion = "0.7b";
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

int main(int argc, char** argv) {
    std::filesystem::path executable;
#if defined(_WIN32)
    std::wstring module_path(32768, L' ');
    const DWORD module_length = GetModuleFileNameW(
        GetModuleHandleW(nullptr), module_path.data(),
        static_cast<DWORD>(module_path.size()));
    if (module_length != 0 && module_length < module_path.size()) {
        module_path.resize(module_length);
        executable = module_path;
    }
#endif
    if (executable.empty() && argc > 0 && argv[0] != nullptr)
        executable = std::filesystem::absolute(argv[0]);
    if (executable.empty())
        executable = std::filesystem::current_path() / "Onix360.exe";

    onix360::storage::DataDirectories directories(
        onix360::storage::DataDirectories::portable_root(executable));
    std::error_code directory_error;
    if (!directories.create(&directory_error)) {
        std::cerr << "Unable to create portable Onix360-data folders: "
                  << directory_error.message() << "\n";
        return 3;
    }
    std::cout << "Onix360 - Windows Xbox 360 Emulator\n"
              << "Version: " << kVersion << "\n"
              << "CPU baseline: x86-64 / SSE2; AVX2 is not required.\n";
    if (!has_sse2()) {
        std::cerr << "This build requires an SSE2-capable CPU.\n";
        return 1;
    }
    onix360::cpu::XenonThreadState thread;
    thread.reset(0x10000);
    const auto result = onix360::cpu::execute(thread, 0x38600001U); // addi r3,r0,1
    std::cout << "Interpreter smoke test: "
              << (result == onix360::cpu::StepResult::Executed ? "instruction executed" : "unsupported")
              << "; r3=" << thread.gpr[3] << "; PC=0x"
              << std::hex << thread.pc << std::dec << "\n"
              << "Status: limited PowerPC interpreter; not a complete Xenon CPU.\n";
    if (result != onix360::cpu::StepResult::Executed || thread.gpr[3] != 1) return 2;
#if defined(_WIN32)
    onix360::ui::AuroraWindow dashboard;
    return dashboard.run(GetModuleHandleW(nullptr), SW_SHOW, directories.roms());
#else
    return 0;
#endif
}
