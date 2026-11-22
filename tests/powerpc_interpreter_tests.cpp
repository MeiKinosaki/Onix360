#include "wx360/cpu/powerpc_interpreter.hpp"

#include <array>
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
constexpr std::uint32_t branch_form(unsigned op, unsigned bo, unsigned bi, unsigned displacement, bool link = false) {
    return (op << 26U) | ((bo & 31U) << 21U) | ((bi & 31U) << 16U) |
           (displacement & 0xFFFCU) | (link ? 1U : 0U);
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

    // ADDIC updates XER[CA] on unsigned carry-out; ADDIC. also records CR0.
    s.gpr[26] = UINT64_MAX;
    check(execute(s, d_form(12, 27, 26, 1)) == StepResult::Executed, "ADDIC executes");
    check(s.gpr[27] == 0 && (s.xer & (std::uint64_t{1} << 29U)) != 0,
          "ADDIC wraps and sets XER carry");
    s.gpr[26] = 7;
    check(execute(s, d_form(13, 27, 26, 1)) == StepResult::Executed, "ADDIC. executes");
    check(s.gpr[27] == 8 && s.condition_field(0) == 0x4, "ADDIC. updates CR0");

    std::array<std::uint8_t, 64> ram{};
    s.gpr[21] = 0x12345678U;
    check(execute(s, d_form(36, 21, 0, 8), ram) == StepResult::Executed, "STW executes");
    check(ram[8] == 0x12 && ram[9] == 0x34 && ram[10] == 0x56 && ram[11] == 0x78,
          "STW stores big-endian word");
    s.gpr[22] = 0;
    check(execute(s, d_form(32, 22, 0, 8), ram) == StepResult::Executed, "LWZ executes");
    check(s.gpr[22] == 0x12345678U, "LWZ loads big-endian word");
    ram[12] = 0xAB;
    check(execute(s, d_form(34, 23, 0, 12), ram) == StepResult::Executed, "LBZ executes");
    check(s.gpr[23] == 0xAB, "LBZ zero extends byte");
    s.gpr[24] = 0xCD;
    check(execute(s, d_form(38, 24, 0, 13), ram) == StepResult::Executed, "STB executes");
    check(ram[13] == 0xCD, "STB stores low byte");

    // Update forms write the effective address back to RA only after success.
    s.gpr[28] = 4;
    check(execute(s, d_form(33, 29, 28, 8), ram) == StepResult::Executed, "LWZU executes");
    check(s.gpr[29] == 0 && s.gpr[28] == 12, "LWZU loads and updates RA");
    s.gpr[28] = 16;
    s.gpr[30] = 0xA1B2C3D4U;
    check(execute(s, d_form(37, 30, 28, 4), ram) == StepResult::Executed, "STWU executes");
    check(s.gpr[28] == 20 && ram[16] == 0xA1 && ram[19] == 0xD4,
          "STWU stores and updates RA");
    s.gpr[28] = 63;
    const auto ra_before_fault = s.gpr[28];
    check(execute(s, d_form(33, 29, 28, 1), ram) == StepResult::MemoryFault,
          "LWZU reports out-of-range access");
    check(s.gpr[28] == ra_before_fault, "LWZU fault leaves RA unchanged");
    const auto before_fault_pc = s.pc;
    check(execute(s, d_form(32, 25, 0, 63), ram) == StepResult::MemoryFault,
          "out-of-range LWZ reports memory fault");
    check(s.pc == before_fault_pc, "memory fault does not advance PC");

    const auto branch_pc = s.pc;
    check(execute(s, (18U << 26U) | 0x10U | 1U) == StepResult::Executed, "B with link executes");
    check(s.pc == branch_pc + 0x10U && s.lr == branch_pc + 4U, "B updates PC and LR");
    const auto bc_pc = s.pc;
    s.set_condition_field(0, 0x2);
    check(execute(s, branch_form(16, 12, 2, 0x20U)) == StepResult::Executed, "BC conditional branch executes");
    check(s.pc == bc_pc + 0x20U, "BC takes branch when CR bit matches BO");
    const auto not_taken_pc = s.pc;
    check(execute(s, branch_form(16, 4, 2, 0x20U)) == StepResult::Executed, "BC not-taken path executes");
    check(s.pc == not_taken_pc + 4U, "BC advances normally when condition fails");

    const auto old_pc = s.pc;
    check(execute(s, 0xFFFFFFFFU) == StepResult::UnsupportedInstruction, "unknown instruction is rejected");
    check(s.pc == old_pc, "unsupported instruction does not advance PC");

    if (failures == 0) std::cout << "WX360 PowerPC interpreter tests passed\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
