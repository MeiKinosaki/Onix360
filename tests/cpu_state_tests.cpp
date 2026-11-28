#include "onix360/cpu/xenon_state.hpp"

#include <cstdlib>
#include <iostream>

namespace {
int failures = 0;
void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}
}

int main() {
    onix360::cpu::XenonThreadState state;
    state.gpr[3] = 0x1234;
    state.lr = 0x88;
    state.set_condition_field(2, 0xB);
    state.reset(0x1000);

    check(state.pc == 0x1000, "reset preserves supplied entry point in PC");
    check(state.gpr[3] == 0, "reset clears GPR bank");
    check(state.lr == 0 && state.ctr == 0 && state.xer == 0, "reset clears SPR state");
    check(state.cr == 0 && state.msr == 0 && state.fpscr == 0, "reset clears control state");

    state.set_condition_field(0, 0xA);
    state.set_condition_field(7, 0x5);
    check(state.condition_field(0) == 0xA, "CR field 0 uses most-significant nibble");
    check(state.condition_field(7) == 0x5, "CR field 7 uses least-significant nibble");
    state.set_condition_field(3, 0x1F);
    check(state.condition_field(3) == 0xF, "CR field is masked to four bits");
    check(state.condition_field(8) == 0, "out-of-range CR read is safe");

    if (failures == 0) std::cout << "Onix360 Xenon state tests passed\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
