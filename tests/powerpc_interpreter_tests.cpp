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
    constexpr std::uint64_t xer_ca = std::uint64_t{1} << 29U;
    XenonThreadState s;
    s.reset(0x1000);
    s.gpr[3] = 10;
    check(execute(s, d_form(14, 4, 3, 5)) == StepResult::Executed, "ADDI executes");
    check(s.gpr[4] == 15 && s.pc == 0x1004, "ADDI writes RT and advances PC");

    check(execute(s, d_form(14, 5, 0, 0xFFFF)) == StepResult::Executed, "ADDI accepts negative immediate");
    check(s.gpr[5] == UINT64_MAX, "ADDI sign-extends immediate");

    // Immediate logical instructions use zero-extended 16-bit immediates.
    s.gpr[1] = 0x1234000000000000ULL;
    check(execute(s, d_form(24, 1, 2, 0x00F0)) == StepResult::Executed, "ORI executes");
    check(s.gpr[2] == 0x12340000000000F0ULL, "ORI zero-extends immediate and writes RA");
    s.gpr[1] = 0x00000000000000F0ULL;
    check(execute(s, d_form(25, 1, 3, 0x0001)) == StepResult::Executed, "ORIS executes");
    check(s.gpr[3] == 0x00000000000100F0ULL, "ORIS shifts immediate by 16");
    s.gpr[1] = 0xFFFF0000000000FFULL;
    check(execute(s, d_form(26, 1, 4, 0x00F0)) == StepResult::Executed, "XORI executes");
    check(s.gpr[4] == 0xFFFF00000000000FULL, "XORI applies zero-extended immediate");
    s.gpr[1] = 0x00000000000100F0ULL;
    check(execute(s, d_form(27, 1, 5, 0x0001)) == StepResult::Executed, "XORIS executes");
    check(s.gpr[5] == 0x00000000000000F0ULL, "XORIS shifts immediate by 16");
    s.gpr[1] = 0x80000000000000FFULL;
    check(execute(s, d_form(28, 1, 6, 0x00F0)) == StepResult::Executed, "ANDI. executes");
    check(s.gpr[6] == 0xF0 && s.condition_field(0) == 0x4, "ANDI. masks and updates CR0");
    s.gpr[1] = 0x80000000000100F0ULL;
    check(execute(s, d_form(29, 1, 7, 0x0001)) == StepResult::Executed, "ANDIS. executes");
    check(s.gpr[7] == 0x0000000000010000ULL && s.condition_field(0) == 0x4,
          "ANDIS. shifts mask and updates CR0");

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

    // Word shifts operate on the low 32 bits and zero/sign extend appropriately.
    s.gpr[1] = 0x0000000080000001ULL;
    s.gpr[2] = 1;
    check(execute(s, x_form(1, 3, 2, 24)) == StepResult::Executed, "SLW executes");
    check(s.gpr[3] == 2, "SLW shifts left and zero-extends word result");
    s.gpr[1] = 0x0000000080000001ULL;
    s.gpr[2] = 1;
    check(execute(s, x_form(1, 4, 2, 536)) == StepResult::Executed, "SRW executes");
    check(s.gpr[4] == 0x40000000ULL, "SRW shifts right logically");
    s.gpr[1] = 0x0000000080000001ULL;
    s.gpr[2] = 32;
    check(execute(s, x_form(1, 5, 2, 24)) == StepResult::Executed, "SLW handles shift >= 32");
    check(s.gpr[5] == 0, "SLW returns zero when RB bit 5 is set");
    s.gpr[1] = 0x00000000FFFFFFF0ULL;
    s.gpr[2] = 2;
    check(execute(s, x_form(1, 6, 2, 792)) == StepResult::Executed, "SRAW executes");
    check(s.gpr[6] == 0xFFFFFFFFFFFFFFFCULL, "SRAW sign-extends arithmetic word result");
    check((s.xer & xer_ca) != 0, "SRAW sets CA when negative one bits are shifted out");
    s.gpr[1] = 0x000000007FFFFFF0ULL;
    s.gpr[2] = 2;
    check(execute(s, x_form(1, 7, 2, 792)) == StepResult::Executed, "SRAW handles positive operand");
    check(s.gpr[7] == 0x1FFFFFFCULL && (s.xer & xer_ca) == 0,
          "SRAW positive result does not set CA");
    s.gpr[1] = 0x00000000FFFFFFF0ULL;
    check(execute(s, rlwinm_form(1, 8, 0, 0, 31)) == StepResult::Executed,
          "RLWINM helper remains usable after shift tests");

    // Doubleword shifts: counts >= 64 produce zero or sign fill.
    s.gpr[1] = 0x8000000000000001ULL;
    s.gpr[2] = 1;
    check(execute(s, x_form(1, 3, 2, 27)) == StepResult::Executed, "SLD executes");
    check(s.gpr[3] == 2, "SLD shifts left 64-bit value");
    s.gpr[1] = 0x8000000000000001ULL;
    s.gpr[2] = 1;
    check(execute(s, x_form(1, 4, 2, 539)) == StepResult::Executed, "SRD executes");
    check(s.gpr[4] == 0x4000000000000000ULL, "SRD shifts right logically");
    s.gpr[1] = 0x8000000000000001ULL;
    s.gpr[2] = 64;
    check(execute(s, x_form(1, 5, 2, 27)) == StepResult::Executed, "SLD handles shift >= 64");
    check(s.gpr[5] == 0, "SLD returns zero for shift >= 64");
    s.gpr[1] = 0xFFFFFFFFFFFFFFF0ULL;
    s.gpr[2] = 2;
    s.xer &= ~xer_ca;
    check(execute(s, x_form(1, 6, 2, 794)) == StepResult::Executed, "SRAD executes");
    check(s.gpr[6] == 0xFFFFFFFFFFFFFFFCULL, "SRAD sign-fills shifted result");
    check((s.xer & xer_ca) != 0, "SRAD sets CA when discarded negative bits contain ones");
    s.gpr[1] = 0x7FFFFFFFFFFFFFF0ULL;
    s.gpr[2] = 2;
    s.xer |= xer_ca;
    check(execute(s, x_form(1, 7, 2, 794)) == StepResult::Executed, "SRAD handles positive value");
    check(s.gpr[7] == 0x1FFFFFFFFFFFFFFCULL && (s.xer & xer_ca) == 0,
          "SRAD positive result clears CA");
    s.gpr[1] = 0x8000000000000000ULL;
    s.gpr[2] = 64;
    check(execute(s, x_form(1, 8, 2, 794)) == StepResult::Executed, "SRAD handles shift >= 64");
    check(s.gpr[8] == UINT64_MAX && (s.xer & xer_ca) == 0,
          "SRAD saturates sign and only sets CA for discarded one bits");

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

    // SUBFIC computes SIMM - RA and sets CA when no borrow occurs.
    s.gpr[2] = 3;
    check(execute(s, d_form(8, 9, 2, 5)) == StepResult::Executed, "SUBFIC executes");
    check(s.gpr[9] == 2 && (s.xer & xer_ca) != 0, "SUBFIC sets carry when no borrow");
    s.gpr[2] = 8;
    check(execute(s, d_form(8, 9, 2, 5)) == StepResult::Executed, "SUBFIC executes with borrow");
    check(s.gpr[9] == UINT64_MAX - 2U && (s.xer & xer_ca) == 0,
          "SUBFIC clears carry when borrow occurs");
    s.gpr[2] = 7;
    check(execute(s, x_form(10, 2, 0, 104)) == StepResult::Executed, "NEG executes");
    check(s.gpr[10] == UINT64_MAX - 6U, "NEG computes two's-complement negation");

    // ADDIC updates XER[CA] on unsigned carry-out; ADDIC. also records CR0.
    s.gpr[26] = UINT64_MAX;
    check(execute(s, d_form(12, 27, 26, 1)) == StepResult::Executed, "ADDIC executes");
    check(s.gpr[27] == 0 && (s.xer & (std::uint64_t{1} << 29U)) != 0,
          "ADDIC wraps and sets XER carry");
    s.gpr[26] = 7;
    check(execute(s, d_form(13, 27, 26, 1)) == StepResult::Executed, "ADDIC. executes");
    check(s.gpr[27] == 8 && s.condition_field(0) == 0x5, "ADDIC. updates CR0 and preserves XER[SO]");

    // ADDE and SUBFE consume and update XER[CA].
    s.gpr[3] = UINT64_MAX;
    s.gpr[4] = 0;
    s.xer |= xer_ca;
    check(execute(s, x_form(5, 3, 4, 138)) == StepResult::Executed, "ADDE executes");
    check(s.gpr[5] == 0 && (s.xer & xer_ca) != 0, "ADDE consumes carry-in and sets carry-out");
    s.gpr[3] = 5;
    s.gpr[4] = 3;
    s.xer |= xer_ca;
    check(execute(s, x_form(6, 3, 4, 136)) == StepResult::Executed, "SUBFE executes");
    check(s.gpr[6] == UINT64_MAX - 1U && (s.xer & xer_ca) == 0,
          "SUBFE computes complemented minuend and clears carry when no unsigned carry-out");
    s.gpr[3] = 5;
    s.gpr[4] = 3;
    s.xer &= ~xer_ca;
    check(execute(s, x_form(6, 3, 4, 136)) == StepResult::Executed, "SUBFE executes without carry-in");
    check(s.gpr[6] == UINT64_MAX - 2U && (s.xer & xer_ca) == 0,
          "SUBFE clears carry-out when borrow occurs");

    // ADDZE and SUBFZE use XER[CA] as their carry-in.
    s.gpr[3] = UINT64_MAX;
    s.xer |= xer_ca;
    check(execute(s, x_form(7, 3, 0, 202)) == StepResult::Executed, "ADDZE executes");
    check(s.gpr[7] == 0 && (s.xer & xer_ca) != 0, "ADDZE wraps and retains carry-out");
    s.gpr[3] = 5;
    s.xer |= xer_ca;
    check(execute(s, x_form(8, 3, 0, 200)) == StepResult::Executed, "SUBFZE executes");
    check(s.gpr[8] == UINT64_MAX - 4U && (s.xer & xer_ca) == 0,
          "SUBFZE computes complement plus carry-in");
    s.gpr[3] = 5;
    s.xer &= ~xer_ca;
    check(execute(s, x_form(8, 3, 0, 200)) == StepResult::Executed, "SUBFZE executes without carry-in");
    check(s.gpr[8] == UINT64_MAX - 5U && (s.xer & xer_ca) == 0,
          "SUBFZE computes complement without carry-in");

    // ADDME and SUBFME compute operand + carry-in - 1.
    s.gpr[3] = 0;
    s.xer |= xer_ca;
    check(execute(s, x_form(9, 3, 0, 234)) == StepResult::Executed, "ADDME executes");
    check(s.gpr[9] == 0 && (s.xer & xer_ca) != 0,
          "ADDME with carry-in one returns zero and sets carry-out");
    s.gpr[3] = 0;
    s.xer &= ~xer_ca;
    check(execute(s, x_form(9, 3, 0, 234)) == StepResult::Executed, "ADDME executes without carry-in");
    check(s.gpr[9] == UINT64_MAX && (s.xer & xer_ca) == 0,
          "ADDME without carry-in wraps to all ones");
    s.gpr[3] = 5;
    s.xer |= xer_ca;
    check(execute(s, x_form(10, 3, 0, 232)) == StepResult::Executed, "SUBFME executes");
    check(s.gpr[10] == UINT64_MAX - 5U && (s.xer & xer_ca) != 0,
          "SUBFME computes complemented operand plus carry-in minus one");

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
    check(s.gpr[28] == 20 && ram[20] == 0xA1 && ram[23] == 0xD4,
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

    // Halfword accesses are big-endian; LHA sign-extends while LHZ zero-extends.
    ram[24] = 0x80;
    ram[25] = 0x01;
    check(execute(s, d_form(40, 16, 0, 24), ram) == StepResult::Executed, "LHZ executes");
    check(s.gpr[16] == 0x8001, "LHZ zero-extends big-endian halfword");
    check(execute(s, d_form(42, 17, 0, 24), ram) == StepResult::Executed, "LHA executes");
    check(s.gpr[17] == 0xFFFFFFFFFFFF8001ULL, "LHA sign-extends big-endian halfword");
    s.gpr[18] = 0x1234;
    check(execute(s, d_form(44, 18, 0, 26), ram) == StepResult::Executed, "STH executes");
    check(ram[26] == 0x12 && ram[27] == 0x34, "STH stores big-endian halfword");
    s.gpr[28] = 28;
    check(execute(s, d_form(41, 19, 28, 0), ram) == StepResult::Executed, "LHZU executes");
    check(s.gpr[19] == 0 && s.gpr[28] == 28, "LHZU loads and updates RA");
    s.gpr[28] = 30;
    s.gpr[20] = 0xBEEF;
    check(execute(s, d_form(45, 20, 28, 0), ram) == StepResult::Executed, "STHU executes");
    check(ram[30] == 0xBE && ram[31] == 0xEF && s.gpr[28] == 30,
          "STHU stores and updates RA");
    s.gpr[28] = 63;
    const auto halfword_ra_before_fault = s.gpr[28];
    check(execute(s, d_form(41, 19, 28, 0), ram) == StepResult::MemoryFault,
          "LHZU reports out-of-range access");
    check(s.gpr[28] == halfword_ra_before_fault, "LHZU fault leaves RA unchanged");

    const auto old_pc = s.pc;
    check(execute(s, 0xFFFFFFFFU) == StepResult::UnsupportedInstruction, "unknown instruction is rejected");
    check(s.pc == old_pc, "unsupported instruction does not advance PC");

    if (failures == 0) std::cout << "WX360 PowerPC interpreter tests passed\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
