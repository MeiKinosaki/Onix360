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

    // CNTLZW counts leading zeroes in the low 32 bits and supports Rc.
    s.gpr[8] = 0x0000000000000001ULL;
    check(execute(s, x_form(8, 9, 0, 26)) == StepResult::Executed, "CNTLZW executes");
    check(s.gpr[9] == 31, "CNTLZW counts 31 leading zeroes");
    s.gpr[8] = 0x0000000080000000ULL;
    check(execute(s, x_form(8, 10, 0, 26)) == StepResult::Executed, "CNTLZW handles top bit set");
    check(s.gpr[10] == 0, "CNTLZW returns zero when bit 31 is set");
    s.gpr[8] = 0;
    s.xer = (std::uint64_t{1} << 31U);
    check(execute(s, x_form(8, 11, 0, 26, true)) == StepResult::Executed,
          "CNTLZW. executes with record bit set");
    check(s.gpr[11] == 32 && s.condition_field(0) == 0x5,
          "CNTLZW. records positive result and preserves XER[SO]");

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
    s.gpr[1] = 0x00000000FFFFFFF3ULL;
    s.gpr[2] = 2;
    check(execute(s, x_form(1, 6, 2, 792)) == StepResult::Executed, "SRAW executes");
    check(s.gpr[6] == 0xFFFFFFFFFFFFFFFCULL, "SRAW sign-extends arithmetic word result");
    check((s.xer & xer_ca) != 0, "SRAW sets CA when negative one bits are shifted out");
    s.gpr[1] = 0x000000007FFFFFF0ULL;
    s.gpr[2] = 2;
    check(execute(s, x_form(1, 7, 2, 792)) == StepResult::Executed, "SRAW handles positive operand");
    check(s.gpr[7] == 0x1FFFFFFCULL && (s.xer & xer_ca) == 0,
          "SRAW positive result does not set CA");

    // SRAWI uses the SH field encoded in the RB position, not GPR[RB].
    s.gpr[1] = 0x00000000FFFFFFF0ULL;
    s.gpr[2] = 0x12345678ULL;
    s.xer &= ~xer_ca;
    const std::uint32_t srawi_shift_zero = (31U << 26U) | (1U << 21U) | (8U << 16U) | (824U << 1U);
    check(execute(s, srawi_shift_zero) == StepResult::Executed,
          "SRAWI executes using immediate shift amount");
    check(s.gpr[8] == 0xFFFFFFFFFFFFFFF0ULL && (s.xer & xer_ca) == 0,
          "SRAWI shift by zero sign-extends and preserves clear CA");
    const std::uint32_t srawi_shift_two = (31U << 26U) | (1U << 21U) |
        (9U << 16U) | (2U << 11U) | (824U << 1U);
    s.gpr[1] = 0x00000000FFFFFFF3ULL;
    check(execute(s, srawi_shift_two) == StepResult::Executed,
          "SRAWI executes with shift amount two");
    check(s.gpr[9] == 0xFFFFFFFFFFFFFFFCULL && (s.xer & xer_ca) != 0,
          "SRAWI sets CA when discarded negative bits contain ones");
    const std::uint32_t srawi_positive_two = (31U << 26U) | (1U << 21U) |
        (10U << 16U) | (2U << 11U) | (824U << 1U);
    s.gpr[1] = 0x000000007FFFFFF3ULL;
    check(execute(s, srawi_positive_two) == StepResult::Executed,
          "SRAWI executes with positive operand");
    check(s.gpr[10] == 0x1FFFFFFCULL && (s.xer & xer_ca) == 0,
          "SRAWI positive operand does not set CA");
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
    s.gpr[1] = 0xFFFFFFFFFFFFFFF3ULL;
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
    check(s.condition_field(2) == 0x9, "CMP sets selected CR field to LT and preserves SO");
    check(execute(s, compare_form(3, true, 17, 18, 32)) == StepResult::Executed, "CMPL unsigned 64-bit executes");
    check(s.condition_field(3) == 0x5, "CMPL sets selected CR field to GT and preserves SO");

    s.gpr[19] = 0x80000001U;
    check(execute(s, rlwinm_form(19, 20, 1, 0, 31, true)) == StepResult::Executed, "RLWINM executes");
    check(s.gpr[20] == 3, "RLWINM rotates 32-bit word and applies full mask");
    check(s.condition_field(0) == 0x5, "RLWINM record form evaluates 32-bit result and preserves SO");

    s.xer = (std::uint64_t{1} << 31U);
    s.gpr[8] = 1; s.gpr[9] = 2;
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

    // Doubleword arithmetic shift must sign-fill and update XER[CA].
    s.gpr[1] = 0x8000000000000003ULL;
    s.gpr[2] = 1;
    s.xer &= ~xer_ca;
    check(execute(s, x_form(1, 11, 2, 794)) == StepResult::Executed,
          "SRAD executes for a negative operand");
    check(s.gpr[11] == 0xC000000000000001ULL && (s.xer & xer_ca) != 0,
          "SRAD sign-fills and sets CA for discarded one bits");
    s.gpr[1] = 0x8000000000000000ULL;
    s.gpr[2] = 1;
    s.xer |= xer_ca;
    check(execute(s, x_form(1, 12, 2, 794)) == StepResult::Executed,
          "SRAD executes when discarded bits are zero");
    check(s.gpr[12] == 0xC000000000000000ULL && (s.xer & xer_ca) == 0,
          "SRAD clears CA when no discarded one bits are shifted out");
    s.gpr[1] = 0x7FFFFFFFFFFFFFFFULL;
    s.gpr[2] = 64;
    check(execute(s, x_form(1, 13, 2, 794)) == StepResult::Executed,
          "SRAD handles shift amount 64");
    check(s.gpr[13] == 0 && (s.xer & xer_ca) == 0,
          "SRAD shift by 64 produces zero for positive operands");
    s.gpr[1] = 0x8000000000000001ULL;
    s.gpr[2] = 64;
    s.xer &= ~xer_ca;
    check(execute(s, x_form(1, 14, 2, 794)) == StepResult::Executed,
          "SRAD handles a negative operand shifted by 64");
    check(s.gpr[14] == UINT64_MAX && (s.xer & xer_ca) != 0,
          "SRAD shift by 64 sign-fills and sets CA for discarded one bits");
    s.gpr[1] = 0x8000000000000000ULL;
    s.xer |= xer_ca;
    check(execute(s, x_form(1, 15, 2, 794)) == StepResult::Executed,
          "SRAD handles negative shift by 64 with no discarded one bits");
    check(s.gpr[15] == UINT64_MAX && (s.xer & xer_ca) == 0,
          "SRAD shift by 64 clears CA when lower bits are all zero");
    s.gpr[1] = 0x8000000000000003ULL;
    s.gpr[2] = 1;
    s.xer &= ~(std::uint64_t{1} << 31U);
    check(execute(s, x_form(1, 16, 2, 794, true)) == StepResult::Executed,
          "SRAD. executes with record bit set");
    check(s.gpr[16] == 0xC000000000000001ULL && s.condition_field(0) == 0x8,
          "SRAD. records the 64-bit negative result in CR0");

    // SRAW variable shifts at and beyond the 32-bit word boundary.
    s.gpr[1] = 0x0000000080000001ULL;
    s.gpr[2] = 32;
    s.xer &= ~xer_ca;
    check(execute(s, x_form(1, 17, 2, 792)) == StepResult::Executed,
          "SRAW executes with shift amount 32");
    check(s.gpr[17] == UINT64_MAX && (s.xer & xer_ca) != 0,
          "SRAW shift by 32 sign-extends and sets CA for discarded ones");
    s.gpr[1] = 0x0000000080000000ULL;
    s.gpr[2] = 63;
    s.xer |= xer_ca;
    check(execute(s, x_form(1, 18, 2, 792)) == StepResult::Executed,
          "SRAW executes with shift amount 63");
    check(s.gpr[18] == UINT64_MAX && (s.xer & xer_ca) == 0,
          "SRAW shift by 63 clears CA when discarded low bits are zero");
    s.gpr[1] = 0x000000007FFFFFFFULL;
    s.gpr[2] = 32;
    check(execute(s, x_form(1, 19, 2, 792)) == StepResult::Executed,
          "SRAW executes for a positive operand at shift 32");
    check(s.gpr[19] == 0 && (s.xer & xer_ca) == 0,
          "SRAW shift by 32 produces zero and clears CA for positive operands");

    // SLW: word shifts at 31 and with RB[5] set (32/63) zero the result.
    s.gpr[1] = 0x0000000080000001ULL;
    s.gpr[2] = 31;
    check(execute(s, x_form(1, 20, 2, 24)) == StepResult::Executed,
          "SLW executes at shift amount 31");
    check(s.gpr[20] == 0x80000000ULL,
          "SLW shift by 31 keeps the low-word result and zero-extends");
    s.gpr[2] = 32;
    check(execute(s, x_form(1, 21, 2, 24)) == StepResult::Executed,
          "SLW executes when RB bit 5 is set");
    check(s.gpr[21] == 0, "SLW shift by 32 produces zero");
    s.gpr[2] = 63;
    check(execute(s, x_form(1, 22, 2, 24)) == StepResult::Executed,
          "SLW executes at shift amount 63");
    check(s.gpr[22] == 0, "SLW shift by 63 produces zero");

    // SRW: word shifts at 31 and with RB[5] set (32/63) zero the result.
    s.gpr[1] = 0x0000000080000001ULL;
    s.gpr[2] = 31;
    check(execute(s, x_form(1, 29, 2, 536)) == StepResult::Executed,
          "SRW executes at shift amount 31");
    check(s.gpr[29] == 1, "SRW shift by 31 is logical and zero-extends");
    s.gpr[2] = 32;
    check(execute(s, x_form(1, 30, 2, 536)) == StepResult::Executed,
          "SRW executes when RB bit 5 is set");
    check(s.gpr[30] == 0, "SRW shift by 32 produces zero");
    s.gpr[2] = 63;
    check(execute(s, x_form(1, 31, 2, 536)) == StepResult::Executed,
          "SRW executes at shift amount 63");
    check(s.gpr[31] == 0, "SRW shift by 63 produces zero");

    // Doubleword logical shifts: 63 is valid; 64 and 127 produce zero.
    s.gpr[1] = 0x8000000000000001ULL;
    s.gpr[2] = 63;
    check(execute(s, x_form(1, 23, 2, 27)) == StepResult::Executed,
          "SLD executes at shift amount 63");
    check(s.gpr[23] == 0x8000000000000000ULL,
          "SLD shift by 63 moves the low bit to the sign position");
    s.gpr[2] = 64;
    check(execute(s, x_form(1, 24, 2, 27)) == StepResult::Executed,
          "SLD executes at shift amount 64");
    check(s.gpr[24] == 0, "SLD shift by 64 produces zero");
    s.gpr[2] = 127;
    check(execute(s, x_form(1, 25, 2, 27)) == StepResult::Executed,
          "SLD executes at shift amount 127");
    check(s.gpr[25] == 0, "SLD shift by 127 produces zero");

    s.gpr[1] = 0x8000000000000001ULL;
    s.gpr[2] = 63;
    check(execute(s, x_form(1, 26, 2, 539)) == StepResult::Executed,
          "SRD executes at shift amount 63");
    check(s.gpr[26] == 1, "SRD shift by 63 keeps the original top bit");
    s.gpr[2] = 64;
    check(execute(s, x_form(1, 27, 2, 539)) == StepResult::Executed,
          "SRD executes at shift amount 64");
    check(s.gpr[27] == 0, "SRD shift by 64 produces zero");
    s.gpr[2] = 127;
    check(execute(s, x_form(1, 28, 2, 539)) == StepResult::Executed,
          "SRD executes at shift amount 127");
    check(s.gpr[28] == 0, "SRD shift by 127 produces zero");

    // SRAWI record form: verify CR0 uses the sign of the 32-bit result.
    s.gpr[1] = 0x0000000080000001ULL;
    s.xer &= ~(std::uint64_t{1} << 31U);
    const std::uint32_t srawi_record_zero = (31U << 26U) | (1U << 21U) | (9U << 16U) | (824U << 1U) | 1U;
    check(execute(s, srawi_record_zero) == StepResult::Executed,
          "SRAWI. executes with record bit set");
    check(s.gpr[9] == 0xFFFFFFFF80000001ULL && s.condition_field(0) == 0x8,
          "SRAWI. records a negative sign-extended word in CR0");
    s.gpr[1] = 0x0000000000000004ULL;
    const std::uint32_t srawi_record_zero_positive = (31U << 26U) | (1U << 21U) | (10U << 16U) | (824U << 1U) | 1U;
    check(execute(s, srawi_record_zero_positive) == StepResult::Executed,
          "SRAWI. executes for a positive word");
    check(s.gpr[10] == 4 && s.condition_field(0) == 0x4,
          "SRAWI. records a positive nonzero result in CR0");

    std::array<std::uint8_t, 64> ram{};
    s.gpr[21] = 0x12345678U;
    check(execute(s, d_form(36, 21, 0, 8), ram) == StepResult::Executed, "STW executes");
    check(ram[8] == 0x12 && ram[9] == 0x34 && ram[10] == 0x56 && ram[11] == 0x78,
          "STW stores big-endian word");
    check(execute(s, d_form(32, 22, 0, 9), ram) == StepResult::AlignmentFault,
          "LWZ rejects a misaligned effective address");
    check(execute(s, d_form(40, 16, 0, 25), ram) == StepResult::AlignmentFault,
          "LHZ rejects a misaligned effective address");
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
    if (s.gpr[29] != 0xABCD0000U || s.gpr[28] != 12)
        std::cerr << "LWZU actual RT=" << std::hex << s.gpr[29]
                  << " RA=" << s.gpr[28] << std::dec << '\n';
    check(s.gpr[29] == 0xABCD0000U && s.gpr[28] == 12, "LWZU loads and updates RA");
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
    check(execute(s, d_form(32, 25, 0, 64), ram) == StepResult::MemoryFault,
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
    s.gpr[28] = 64;
    const auto halfword_ra_before_fault = s.gpr[28];
    check(execute(s, d_form(41, 19, 28, 0), ram) == StepResult::MemoryFault,
          "LHZU reports out-of-range access");
    check(s.gpr[28] == halfword_ra_before_fault, "LHZU fault leaves RA unchanged");

    const auto old_pc = s.pc;
    check(execute(s, 0xFFFFFFFFU) == StepResult::UnsupportedInstruction, "unknown instruction is rejected");
    check(s.pc == old_pc, "unsupported instruction does not advance PC");

    // Additional integer ISA coverage used by Xenon game code.
    s.gpr[1] = 0xF0F0F0F0F0F0F0F0ULL;
    s.gpr[2] = 0x0FF00FF00FF00FF0ULL;
    check(execute(s, x_form(1, 3, 2, 60)) == StepResult::Executed, "ANDC executes");
    check(s.gpr[3] == 0xF000F000F000F000ULL, "ANDC clears bits selected by RB");
    check(execute(s, x_form(1, 4, 2, 124)) == StepResult::Executed, "NOR executes");
    check(s.gpr[4] == 0x000F000F000F000FULL, "NOR complements bitwise OR");
    s.gpr[1] = 0x80;
    check(execute(s, x_form(1, 5, 0, 954)) == StepResult::Executed, "EXTSB executes");
    check(s.gpr[5] == 0xFFFFFFFFFFFFFF80ULL, "EXTSB sign extends low byte");
    s.gpr[1] = 0x8001;
    check(execute(s, x_form(1, 6, 0, 922)) == StepResult::Executed, "EXTSH executes");
    check(s.gpr[6] == 0xFFFFFFFFFFFF8001ULL, "EXTSH sign extends low halfword");
    s.gpr[1] = 0x0000000100000000ULL;
    check(execute(s, x_form(1, 7, 0, 58)) == StepResult::Executed, "CNTLZD executes");
    check(s.gpr[7] == 31, "CNTLZD counts leading zeroes across 64 bits");
    s.gpr[1] = 0;
    check(execute(s, x_form(1, 8, 0, 58)) == StepResult::Executed, "CNTLZD handles zero");
    check(s.gpr[8] == 64, "CNTLZD returns 64 for zero");

    // RFI restores the saved execution context and resumes at SRR0.
    s.pc = 0x700;
    s.srr0 = 0x12345678;
    s.srr1 = 0x0000000000009030ULL;
    s.msr = 0;
    s.exception_pending = true;
    s.exception_vector = 0x700;
    const std::uint32_t rfi = (19U << 26U) | (50U << 1U);
    check(execute(s, rfi) == StepResult::Executed, "RFI executes");
    check(s.pc == 0x12345678 && s.msr == 0x9030,
          "RFI restores SRR0 and SRR1 without incrementing restored PC");
    check(!s.exception_pending && s.exception_vector == 0,
          "RFI clears pending exception state");

    // SPR and condition-register moves used by compiled PowerPC code.
    const auto encode_spr = [](unsigned rt, unsigned spr, unsigned xo) {
        return (31U << 26U) | (rt << 21U) | ((spr & 0x1FU) << 16U) |
               ((spr >> 5U) << 11U) | (xo << 1U);
    };
    s.lr = 0x1122334455667788ULL;
    check(execute(s, encode_spr(4, 8, 339)) == StepResult::Executed, "MFSPR reads LR");
    check(s.gpr[4] == s.lr, "MFSPR copies LR to RT");
    s.ctr = 0x8877665544332211ULL;
    check(execute(s, encode_spr(5, 9, 339)) == StepResult::Executed, "MFSPR reads CTR");
    check(s.gpr[5] == s.ctr, "MFSPR copies CTR to RT");
    s.gpr[6] = 0xAABBCCDDEEFF0011ULL;
    check(execute(s, encode_spr(6, 8, 467)) == StepResult::Executed, "MTSPR writes LR");
    check(s.lr == s.gpr[6], "MTSPR copies RS to LR");
    s.gpr[7] = 0x123456789ABCDEF0ULL;
    check(execute(s, encode_spr(7, 9, 467)) == StepResult::Executed, "MTSPR writes CTR");
    check(s.ctr == s.gpr[7], "MTSPR copies RS to CTR");
    s.cr = 0x12345678U;
    check(execute(s, (31U << 26U) | (8U << 21U) | (19U << 1U)) == StepResult::Executed,
          "MFCR executes");
    check(s.gpr[8] == 0x12345678U, "MFCR copies complete CR to RT");
    s.gpr[9] = 0xA5B6C7D8U;
    const std::uint32_t mtcrf_cr0_cr2 = (31U << 26U) | (9U << 21U) |
        (0xA0U << 12U) | (144U << 1U);
    check(execute(s, mtcrf_cr0_cr2) == StepResult::Executed, "MTCRF executes");
    check(s.condition_field(0) == 0xA && s.condition_field(2) == 0xB &&
          s.condition_field(1) == 0x3, "MTCRF updates only selected CR fields");
    const auto unsupported_spr_pc = s.pc;
    check(execute(s, encode_spr(10, 268, 339)) == StepResult::UnsupportedInstruction,
          "MFSPR rejects unsupported SPR without fabricating a value");
    check(s.pc == unsupported_spr_pc, "unsupported MFSPR leaves PC unchanged");

    if (failures == 0) std::cout << "WX360 PowerPC interpreter tests passed\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
