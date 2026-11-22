#include <cstdint>
#include <iostream>
#include <string_view>

namespace {
constexpr std::string_view kVersion = "0.1b";

bool has_sse2() noexcept {
#if defined(_M_X64) || defined(__x86_64__)
    return true; // SSE2 is mandatory in the x86-64 architecture baseline.
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
} // namespace

int main() {
    std::cout << "WX360 - Windows Xbox 360 Emulator\n"
              << "Version: " << kVersion << "\n"
              << "CPU baseline: x86-64 / SSE2; AVX2 is not required.\n";

    if (!has_sse2()) {
        std::cerr << "This build requires an SSE2-capable CPU.\n";
        return 1;
    }

    std::cout << "Runtime check: SSE2 available.\n"
              << "Status: foundation only; Xenon emulation is not implemented yet.\n";
    return 0;
}
