#include "wx360/cpu/powerpc_interpreter.hpp"

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
constexpr std::uint32_t d_form(unsigned op, unsigned rt, unsigned ra, unsigned imm) {
    return (op << 26U) | (rt << 21U) | (ra << 16U) | (imm & 0xFFFFU);
}
constexpr std::uint32_t compare_form(unsigned bf, bool is64, unsigned ra, unsigned rb, unsigned xo) {
    return (31U << 26U) | ((bf & 7U) << 23U) | (static_cast<unsigned>(is64) << 21U) |
           (ra << 16U) | (rb << 11U) | (xo << 1U);
}
constexpr std::uint32_t rlwinm_form(unsigned rs, unsigned ra, unsigned sh, unsigned mb, unsigned me, bool rc = false) {
    return (21U << 26U) | (rs << 21U) | (ra << 16U) | (sh << 11U) |
           (mb << 6U) | (me << 1U) | (rc ? 1U : 0U);
}
constexpr std::uint32_t x_form(unsigned rt, unsigned ra, unsigned rb, unsigned xo, bool rc = false) {
    return (31U << 26U) | (rt << 21U) | (ra << 16U) |
           (rb << 11U) | (xo << 1U) | (rc ? 1U : 0U);
}
}

int main() {
    using namespace wx360::cpu;
    XenonThreadState s;
    s.reset(0x1000);
    s.gpr[3] = 10;
    check(execute(s, d_form(14, 4, 3, 5)) == StepResult::Executed, "ADDI executes");
    check(s.gpr[4] == 15 && s.pc == 0x1004, "ADDI writes RT and advances PC");

    check(execute(s, d_form(14, 5, 0, 0xFFFF)) == StepResult::Executed, "ADDI accepts negative immediate");
    check(s.gpr[5] == UINT64_MAX, "ADDI sign-extends immediate");

    s.gpr[6] = 2;
    check(execute(s, d_form(15, 7, 6, 1)) == StepResult::Executed, "ADDIS executes");
    check(s.gpr[7] == 0x10002, "ADDIS shifts signed immediate by 16");

    s.gpr[8] = 0xF0;
    s.gpr[9] = 0xCC;
    check(execute(s, x_form(8, 10, 9, 444)) == StepResult::Executed, "OR executes");
    check(s.gpr[10] == 0xFC, "OR computes bitwise result");
    check(execute(s, x_form(8, 11, 9, 316)) == StepResult::Executed, "XOR executes");
    check(s.gpr[11] == 0x3C, "XOR computes bitwise result");
    check(execute(s, x_form(8, 12, 9, 28)) == StepResult::Executed, "AND executes");
    check(s.gpr[12] == 0xC0, "AND computes bitwise result");

    s.gpr[13] = 20;
    s.gpr[14] = 22;
    check(execute(s, x_form(15, 13, 14, 266)) == StepResult::Executed, "ADD executes");
    check(s.gpr[15] == 42, "ADD computes sum");
    check(execute(s, x_form(15, 13, 14, 40)) == StepResult::Executed, "SUBF executes");
    check(s.gpr[15] == 2, "SUBF computes RB minus RA");

    s.gpr[17] = 0xFFFFFFFFFFFFFFF0ULL;
    s.gpr[18] = 5;
    check(execute(s, compare_form(2, true, 17, 18, 0)) == StepResult::Executed, "CMP signed 64-bit executes");
    check(s.condition_field(2) == 0x8, "CMP sets selected CR field to LT");
    check(execute(s, compare_form(3, true, 17, 18, 32)) == StepResult::Executed, "CMPL unsigned 64-bit executes");
    check(s.condition_field(3) == 0x4, "CMPL sets selected CR field to GT");

    s.gpr[19] = 0x80000001U;
    check(execute(s, rlwinm_form(19, 20, 1, 0, 31, true)) == StepResult::Executed, "RLWINM executes");
    check(s.gpr[20] == 3, "RLWINM rotates 32-bit word and applies full mask");
    check(s.condition_field(0) == 0x4, "RLWINM record form evaluates 32-bit result");

    s.xer = (std::uint64_t{1} << 31U);
    check(execute(s, x_form(8, 16, 9, 444, true)) == StepResult::Executed, "OR. record form executes");
    check(s.condition_field(0) == 0x5, "record form updates CR0 GT and SO");

    const auto old_pc = s.pc;
    check(execute(s, 0xFFFFFFFFU) == StepResult::UnsupportedInstruction, "unknown instruction is rejected");
    check(s.pc == old_pc, "unsupported instruction does not advance PC");

    if (failures == 0) std::cout << "WX360 PowerPC interpreter tests passed\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
