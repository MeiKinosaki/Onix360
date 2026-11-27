#include "wx360/cpu/powerpc_interpreter.hpp"

#include <array>
#include <cstdlib>
#include <iostream>

int main() {
    using namespace wx360::cpu;
    int failures = 0;
    const auto check = [&](bool ok, const char* label) {
        if (!ok) { std::cerr << "FAIL: " << label << '\n'; ++failures; }
    };
    const auto d_form = [](unsigned op, unsigned rt, unsigned ra, unsigned imm) {
        return (op << 26U) | (rt << 21U) | (ra << 16U) | (imm & 0xFFFFU);
    };

    std::array<std::uint8_t, 8> ram{};
    XenonThreadState state{};
    state.reset(0x1000);
    check(execute_with_exception_delivery(state, d_form(32, 3, 0, 12), ram) ==
          StepResult::MemoryFault, "out-of-range LWZ reports memory fault");
    check(state.exception_pending && state.pc == 0x300U &&
          state.srr0 == 0x1000U && state.dar == 12U,
          "data-storage exception saves PC and fault address");

    state.reset(0x2000);
    check(execute_with_exception_delivery(state, d_form(40, 4, 0, 1), ram) ==
          StepResult::AlignmentFault, "unaligned LHZ reports alignment fault");
    check(state.exception_pending && state.pc == 0x600U &&
          state.srr0 == 0x2000U && state.dar == 1U,
          "alignment exception enters vector and records DAR");

    state.reset(0x3000);
    check(execute_with_exception_delivery(state, d_form(32, 3, 0, 0), ram) ==
          StepResult::Executed && !state.exception_pending && state.pc == 0x3004U,
          "successful memory access does not raise an exception");

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
