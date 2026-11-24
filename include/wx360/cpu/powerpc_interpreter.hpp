#pragma once

#include "wx360/cpu/xenon_state.hpp"

#include <cstdint>
#include <span>

namespace wx360::cpu {

enum class StepResult {
    Executed,
    UnsupportedInstruction,
    MemoryFault,
    AlignmentFault,
};

struct DecodedInstruction {
    std::uint32_t raw{};
    std::uint8_t primary_opcode{};
    std::uint8_t rt{};
    std::uint8_t ra{};
    std::uint8_t rb{};
    std::uint16_t xo{};
    std::int16_t immediate{};
    bool record{};
};

[[nodiscard]] constexpr std::uint64_t rotate_left_32(std::uint32_t value, unsigned shift) noexcept {
    shift &= 31U;
    if (shift == 0) return value;
    return static_cast<std::uint32_t>((value << shift) | (value >> (32U - shift)));
}

[[nodiscard]] constexpr std::uint32_t mask32(unsigned begin, unsigned end) noexcept {
    std::uint32_t mask = 0;
    for (unsigned bit = begin & 31U;; bit = (bit + 1U) & 31U) {
        mask |= (std::uint32_t{1} << (31U - bit));
        if (bit == (end & 31U)) break;
    }
    return mask;
}

inline void update_cr0(XenonThreadState& state, std::uint64_t value,
                       std::uint64_t sign_bit) noexcept {
    std::uint8_t field = 0;
    if ((value & sign_bit) != 0) field |= 0x8U;
    else if (value != 0) field |= 0x4U;
    else field |= 0x2U;
    if ((state.xer & (std::uint64_t{1} << 31U)) != 0) field |= 0x1U;
    state.set_condition_field(0, field);
}

[[nodiscard]] constexpr DecodedInstruction decode(std::uint32_t word) noexcept {
    return {
        word,
        static_cast<std::uint8_t>(word >> 26U),
        static_cast<std::uint8_t>((word >> 21U) & 0x1FU),
        static_cast<std::uint8_t>((word >> 16U) & 0x1FU),
        static_cast<std::uint8_t>((word >> 11U) & 0x1FU),
        static_cast<std::uint16_t>((word >> 1U) & 0x3FFU),
        static_cast<std::int16_t>(word & 0xFFFFU),
        (word & 1U) != 0
    };
}

[[nodiscard]] inline StepResult execute(
    XenonThreadState& state, std::uint32_t instruction,
    std::span<std::uint8_t> memory = {},
    std::uint64_t* fault_address = nullptr) noexcept {
    const auto d = decode(instruction);
    std::uint64_t result = 0;
    bool writes_result = false;
    bool record_32bit = false;
    std::uint8_t destination = d.rt;
    bool branch_taken = false;
    std::uint64_t branch_target = 0;
    bool update_base = false;
    std::uint64_t updated_address = 0;

    switch (d.primary_opcode) {
    case 18: { // b
        std::int32_t displacement = static_cast<std::int32_t>(instruction & 0x03FFFFFCU);
        if ((displacement & 0x02000000) != 0) displacement |= static_cast<std::int32_t>(0xFC000000U);
        const bool absolute = (instruction & 2U) != 0;
        const bool link = (instruction & 1U) != 0;
        branch_target = absolute ? static_cast<std::uint64_t>(static_cast<std::int64_t>(displacement))
                                 : static_cast<std::uint64_t>(static_cast<std::int64_t>(state.pc) + displacement);
        if (link) state.lr = state.pc + 4U;
        branch_taken = true;
        break;
    }
    case 16: { // bc
        const unsigned bo = (instruction >> 21U) & 0x1FU;
        const unsigned bi = (instruction >> 16U) & 0x1FU;
        bool condition_ok = (bo & 0x10U) != 0;
        if (!condition_ok) {
            const unsigned cr_bit = (state.cr >> (31U - bi)) & 1U;
            condition_ok = cr_bit == ((bo >> 3U) & 1U);
        }
        bool count_ok = true;
        if ((bo & 0x04U) == 0) {
            --state.ctr;
            count_ok = ((state.ctr != 0) != ((bo & 0x02U) != 0));
        }
        const std::int32_t displacement = static_cast<std::int16_t>(instruction & 0xFFFCU);
        const bool absolute = (instruction & 2U) != 0;
        const bool link = (instruction & 1U) != 0;
        branch_taken = condition_ok && count_ok;
        if (branch_taken) {
            branch_target = absolute ? static_cast<std::uint64_t>(static_cast<std::int64_t>(displacement))
                                     : static_cast<std::uint64_t>(static_cast<std::int64_t>(state.pc) + displacement);
        }
        if (link) state.lr = state.pc + 4U;
        break;
    }
    case 32: case 33: case 34: case 35: case 36: case 37: case 38: case 39:
    case 40: case 41: case 42: case 43: case 44: case 45: { // D-form byte/halfword/word loads and stores
        const std::int64_t displacement = d.immediate;
        const std::uint64_t base = d.ra == 0 ? 0 : state.gpr[d.ra];
        const std::uint64_t address = base + static_cast<std::uint64_t>(displacement);
        const bool word_access = d.primary_opcode == 32 || d.primary_opcode == 33 ||
                                 d.primary_opcode == 36 || d.primary_opcode == 37;
        const bool halfword_access = d.primary_opcode >= 40 && d.primary_opcode <= 45;
        const bool load = d.primary_opcode <= 35 || (d.primary_opcode >= 40 && d.primary_opcode <= 43);
        const bool update = d.primary_opcode == 33 || d.primary_opcode == 35 ||
                            d.primary_opcode == 37 || d.primary_opcode == 39 ||
                            d.primary_opcode == 41 || d.primary_opcode == 43 || d.primary_opcode == 45;
        if (update && d.ra == 0) return StepResult::UnsupportedInstruction;
        if (update && load && d.ra == d.rt) return StepResult::UnsupportedInstruction;
        const std::size_t width = word_access ? 4U : (halfword_access ? 2U : 1U);
        if (width > 1U && (address % width) != 0U) {
            if (fault_address) *fault_address = address;
            return StepResult::AlignmentFault;
        }
        if (address > memory.size() || width > memory.size() - static_cast<std::size_t>(address)) {
            if (fault_address) *fault_address = address;
            return StepResult::MemoryFault;
        }
        const auto offset = static_cast<std::size_t>(address);
        if (d.primary_opcode == 32 || d.primary_opcode == 33) {
            result = (static_cast<std::uint32_t>(memory[offset]) << 24U) |
                     (static_cast<std::uint32_t>(memory[offset + 1]) << 16U) |
                     (static_cast<std::uint32_t>(memory[offset + 2]) << 8U) |
                     static_cast<std::uint32_t>(memory[offset + 3]);
            writes_result = true;
        } else if (d.primary_opcode == 34 || d.primary_opcode == 35) {
            result = memory[offset];
            writes_result = true;
        } else if (d.primary_opcode == 40 || d.primary_opcode == 41 ||
                   d.primary_opcode == 42 || d.primary_opcode == 43) {
            const std::uint16_t value = static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(memory[offset]) << 8U) | memory[offset + 1]);
            result = (d.primary_opcode == 42 || d.primary_opcode == 43)
                ? static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int16_t>(value)))
                : value;
            writes_result = true;
        } else if (d.primary_opcode == 44 || d.primary_opcode == 45) {
            const std::uint16_t value = static_cast<std::uint16_t>(state.gpr[d.rt]);
            memory[offset] = static_cast<std::uint8_t>(value >> 8U);
            memory[offset + 1] = static_cast<std::uint8_t>(value);
        } else if (d.primary_opcode == 36 || d.primary_opcode == 37) {
            const std::uint32_t value = static_cast<std::uint32_t>(state.gpr[d.rt]);
            memory[offset] = static_cast<std::uint8_t>(value >> 24U);
            memory[offset + 1] = static_cast<std::uint8_t>(value >> 16U);
            memory[offset + 2] = static_cast<std::uint8_t>(value >> 8U);
            memory[offset + 3] = static_cast<std::uint8_t>(value);
        } else {
            memory[offset] = static_cast<std::uint8_t>(state.gpr[d.rt]);
        }
        if (update) { update_base = true; updated_address = address; }
        break;
    }
    case 8: { // subfic: RT = SIMM - RA; XER[CA] indicates no borrow
        const std::uint64_t lhs = static_cast<std::uint64_t>(static_cast<std::int64_t>(d.immediate));
        const std::uint64_t rhs = state.gpr[d.ra];
        result = lhs - rhs;
        constexpr std::uint64_t xer_ca = std::uint64_t{1} << 29U;
        state.xer = lhs >= rhs ? (state.xer | xer_ca) : (state.xer & ~xer_ca);
        writes_result = true;
        break;
    }
    case 12: case 13: { // addic / addic.
        const std::uint64_t lhs = state.gpr[d.ra];
        const std::uint64_t rhs = static_cast<std::uint64_t>(static_cast<std::int64_t>(d.immediate));
        result = lhs + rhs;
        const bool carry = result < lhs;
        constexpr std::uint64_t xer_ca = std::uint64_t{1} << 29U;
        state.xer = carry ? (state.xer | xer_ca) : (state.xer & ~xer_ca);
        writes_result = true;
        // ADDIC. updates CR0; ADDIC does not.
        if (d.primary_opcode == 13) update_cr0(state, result, std::uint64_t{1} << 63U);
        break;
    }
    case 24: case 25: case 26: case 27: case 28: case 29: { // immediate logical operations
        const std::uint64_t imm = static_cast<std::uint16_t>(instruction & 0xFFFFU);
        const std::uint64_t shifted_imm = imm << ((d.primary_opcode == 25 ||
                                                   d.primary_opcode == 27 ||
                                                   d.primary_opcode == 29) ? 16U : 0U);
        const std::uint64_t source = state.gpr[d.rt];
        switch (d.primary_opcode) {
        case 24: case 25: // ori / oris
            result = source | shifted_imm;
            break;
        case 26: case 27: // xori / xoris
            result = source ^ shifted_imm;
            break;
        case 28: case 29: // andi. / andis.
            result = source & shifted_imm;
            break;
        default:
            return StepResult::UnsupportedInstruction;
        }
        destination = d.ra;
        writes_result = true;
        // ANDI. and ANDIS. always update CR0; they have no Rc bit.
        if (d.primary_opcode == 28 || d.primary_opcode == 29)
            update_cr0(state, result, std::uint64_t{1} << 63U);
        break;
    }
    case 14: // addi
        result = state.gpr[d.ra] +
                 static_cast<std::uint64_t>(static_cast<std::int64_t>(d.immediate));
        writes_result = true;
        break;
    case 15: // addis
        result = state.gpr[d.ra] +
                 (static_cast<std::uint64_t>(static_cast<std::int64_t>(d.immediate)) << 16U);
        writes_result = true;
        break;
    case 21: { // rlwinm
        const unsigned shift = (instruction >> 11U) & 0x1FU;
        const unsigned mb = (instruction >> 6U) & 0x1FU;
        const unsigned me = (instruction >> 1U) & 0x1FU;
        const auto rotated = static_cast<std::uint32_t>(rotate_left_32(
            static_cast<std::uint32_t>(state.gpr[d.rt]), shift));
        result = rotated & mask32(mb, me);
        destination = d.ra;
        writes_result = true;
        record_32bit = true;
        break;
    }
    case 19: { // XL-form privileged return from interrupt (RFI), XO=50.
        if (d.xo != 50U) return StepResult::UnsupportedInstruction;
        branch_target = state.srr0;
        state.msr = state.srr1;
        state.clear_exception();
        branch_taken = true;
        break;
    }
    case 31:
        switch (d.xo) {
        case 19: // mfcr: RT = CR
            result = state.cr;
            writes_result = true;
            break;
        case 144: { // mtcrf: copy selected CR fields from RS
            const unsigned field_mask = (instruction >> 12U) & 0xFFU;
            const std::uint32_t source = static_cast<std::uint32_t>(state.gpr[d.rt]);
            for (unsigned field = 0; field < 8; ++field) {
                if ((field_mask & (1U << (7U - field))) != 0) {
                    const unsigned shift = 28U - field * 4U;
                    state.set_condition_field(field, static_cast<std::uint8_t>((source >> shift) & 0xFU));
                }
            }
            break;
        }
        case 339: { // mfspr: supported Xenon-visible SPRs
            const unsigned spr = (((instruction >> 16U) & 0x1FU) << 5U) |
                                 ((instruction >> 11U) & 0x1FU);
            switch (spr) {
            case 1: result = state.xer; break; // XER
            case 8: result = state.lr; break;  // LR
            case 9: result = state.ctr; break; // CTR
            default: return StepResult::UnsupportedInstruction;
            }
            writes_result = true;
            break;
        }
        case 467: { // mtspr: supported Xenon-visible SPRs
            const unsigned spr = (((instruction >> 16U) & 0x1FU) << 5U) |
                                 ((instruction >> 11U) & 0x1FU);
            const std::uint64_t source = state.gpr[d.rt];
            switch (spr) {
            case 1: state.xer = source; break; // XER
            case 8: state.lr = source; break;  // LR
            case 9: state.ctr = source; break; // CTR
            default: return StepResult::UnsupportedInstruction;
            }
            break;
        }
    case 31:
        switch (d.xo) {
        case 0: { // cmp / cmpd / cmpw
            const unsigned bf = (instruction >> 23U) & 7U;
            const bool is_64 = ((instruction >> 21U) & 1U) != 0;
            const std::uint64_t lhs = is_64 ? state.gpr[d.ra] :
                static_cast<std::uint32_t>(state.gpr[d.ra]);
            const std::uint64_t rhs = is_64 ? state.gpr[d.rb] :
                static_cast<std::uint32_t>(state.gpr[d.rb]);
            const bool less = is_64
                ? static_cast<std::int64_t>(lhs) < static_cast<std::int64_t>(rhs)
                : static_cast<std::int32_t>(lhs) < static_cast<std::int32_t>(rhs);
            const bool greater = is_64
                ? static_cast<std::int64_t>(lhs) > static_cast<std::int64_t>(rhs)
                : static_cast<std::int32_t>(lhs) > static_cast<std::int32_t>(rhs);
            std::uint8_t field = less ? 0x8U : (greater ? 0x4U : 0x2U);
            if ((state.xer & (std::uint64_t{1} << 31U)) != 0) field |= 0x1U;
            state.set_condition_field(bf, field);
            break;
        }
        case 32: { // cmpl / cmpld / cmplw
            const unsigned bf = (instruction >> 23U) & 7U;
            const bool is_64 = ((instruction >> 21U) & 1U) != 0;
            const std::uint64_t lhs = is_64 ? state.gpr[d.ra] :
                static_cast<std::uint32_t>(state.gpr[d.ra]);
            const std::uint64_t rhs = is_64 ? state.gpr[d.rb] :
                static_cast<std::uint32_t>(state.gpr[d.rb]);
            const std::uint8_t field = lhs < rhs ? 0x8U : (lhs > rhs ? 0x4U : 0x2U);
            state.set_condition_field(bf, field | ((state.xer >> 31U) & 1U));
            break;
        }
        case 24: { // slw: shift left word; RB[5] forces zero
            const std::uint32_t value = static_cast<std::uint32_t>(state.gpr[d.rt]);
            const unsigned shift = static_cast<unsigned>(state.gpr[d.rb] & 0x3FU);
            result = (shift & 0x20U) != 0 ? 0U : static_cast<std::uint32_t>(value << shift);
            destination = d.ra;
            writes_result = true;
            record_32bit = true;
            break;
        }
        case 536: { // srw: logical shift right word; RB[5] forces zero
            const std::uint32_t value = static_cast<std::uint32_t>(state.gpr[d.rt]);
            const unsigned shift = static_cast<unsigned>(state.gpr[d.rb] & 0x3FU);
            result = (shift & 0x20U) != 0 ? 0U : (value >> shift);
            destination = d.ra;
            writes_result = true;
            record_32bit = true;
            break;
        }
        case 792: case 824: { // sraw / srawi: arithmetic shift right word
            const std::uint32_t raw = static_cast<std::uint32_t>(state.gpr[d.rt]);
            const std::int32_t value = static_cast<std::int32_t>(raw);
            const unsigned shift = d.xo == 792
                ? static_cast<unsigned>(state.gpr[d.rb] & 0x3FU)
                : static_cast<unsigned>((instruction >> 11U) & 0x1FU);
            const bool negative = value < 0;
            bool shifted_out_one = false;
            if (shift == 0) {
                result = static_cast<std::uint64_t>(static_cast<std::int64_t>(value));
            } else if (shift >= 32U) {
                result = negative ? UINT64_MAX : 0U;
                shifted_out_one = negative && (raw & 0x7FFFFFFFU) != 0;
            } else {
                const std::uint32_t discarded_mask = (std::uint32_t{1} << shift) - 1U;
                shifted_out_one = negative && (raw & discarded_mask) != 0;
                result = static_cast<std::uint64_t>(static_cast<std::int64_t>(value >> shift));
            }
            constexpr std::uint64_t xer_ca = std::uint64_t{1} << 29U;
            state.xer = shifted_out_one ? (state.xer | xer_ca) : (state.xer & ~xer_ca);
            destination = d.ra;
            writes_result = true;
            record_32bit = true;
            break;
        }
        case 27: { // sld: shift left doubleword; RB[5:6] force zero
            const unsigned shift = static_cast<unsigned>(state.gpr[d.rb] & 0x7FU);
            result = shift >= 64U ? 0U : (state.gpr[d.rt] << shift);
            destination = d.ra;
            writes_result = true;
            break;
        }
        case 539: { // srd: logical shift right doubleword
            const unsigned shift = static_cast<unsigned>(state.gpr[d.rb] & 0x7FU);
            result = shift >= 64U ? 0U : (state.gpr[d.rt] >> shift);
            destination = d.ra;
            writes_result = true;
            break;
        }
        case 794: { // srad: arithmetic shift right doubleword
            const std::uint64_t value = state.gpr[d.rt];
            const unsigned shift = static_cast<unsigned>(state.gpr[d.rb] & 0x7FU);
            const bool negative = (value >> 63U) != 0;
            bool shifted_out_one = false;
            if (shift == 0U) {
                result = value;
            } else if (shift >= 64U) {
                result = negative ? UINT64_MAX : 0U;
                shifted_out_one = negative && (value & 0x7FFFFFFFFFFFFFFFULL) != 0;
            } else {
                const std::uint64_t discarded_mask = (std::uint64_t{1} << shift) - 1U;
                shifted_out_one = negative && (value & discarded_mask) != 0;
                result = value >> shift;
                if (negative) result |= (UINT64_MAX << (64U - shift));
            }
            constexpr std::uint64_t xer_ca = std::uint64_t{1} << 29U;
            state.xer = shifted_out_one ? (state.xer | xer_ca) : (state.xer & ~xer_ca);
            destination = d.ra;
            writes_result = true;
            break;
        }
        case 26: { // cntlzw: count leading zero bits in the low 32-bit word
            const std::uint32_t value = static_cast<std::uint32_t>(state.gpr[d.rt]);
            unsigned count = 0;
            while (count < 32U && (value & (std::uint32_t{1} << (31U - count))) == 0)
                ++count;
            result = count;
            destination = d.ra;
            writes_result = true;
            record_32bit = true;
            break;
        }
        case 28:  // and
            result = state.gpr[d.rt] & state.gpr[d.rb];
            destination = d.ra;
            writes_result = true;
            break;
        case 60: // andc
            result = state.gpr[d.rt] & ~state.gpr[d.rb];
            destination = d.ra;
            writes_result = true;
            break;
        case 124: // nor
            result = ~(state.gpr[d.rt] | state.gpr[d.rb]);
            destination = d.ra;
            writes_result = true;
            break;
        case 954: // extsb
            result = static_cast<std::uint64_t>(
                static_cast<std::int64_t>(static_cast<std::int8_t>(state.gpr[d.rt])));
            destination = d.ra;
            writes_result = true;
            break;
        case 922: // extsh
            result = static_cast<std::uint64_t>(
                static_cast<std::int64_t>(static_cast<std::int16_t>(state.gpr[d.rt])));
            destination = d.ra;
            writes_result = true;
            break;
        case 58: { // cntlzd
            const std::uint64_t value = state.gpr[d.rt];
            unsigned count = 0;
            while (count < 64U && (value & (std::uint64_t{1} << (63U - count))) == 0)
                ++count;
            result = count;
            destination = d.ra;
            writes_result = true;
            break;
        }
        case 104: { // neg (OE=0 only): RT = -RA
            if ((instruction & (1U << 10U)) != 0) return StepResult::UnsupportedInstruction;
            result = std::uint64_t{0} - state.gpr[d.ra];
            destination = d.rt;
            writes_result = true;
            break;
        }
        case 138: { // adde (OE=0 only): RA + RB + XER[CA]
            if ((instruction & (1U << 10U)) != 0) return StepResult::UnsupportedInstruction;
            const std::uint64_t lhs = state.gpr[d.ra];
            const std::uint64_t rhs = state.gpr[d.rb];
            const std::uint64_t carry_in = (state.xer >> 29U) & 1U;
            const std::uint64_t partial = lhs + rhs;
            const bool carry1 = partial < lhs;
            result = partial + carry_in;
            const bool carry2 = result < partial;
            constexpr std::uint64_t xer_ca = std::uint64_t{1} << 29U;
            state.xer = (carry1 || carry2) ? (state.xer | xer_ca) : (state.xer & ~xer_ca);
            destination = d.rt;
            writes_result = true;
            break;
        }
        case 136: { // subfe (OE=0 only): ~RA + RB + XER[CA]
            if ((instruction & (1U << 10U)) != 0) return StepResult::UnsupportedInstruction;
            const std::uint64_t lhs = ~state.gpr[d.ra];
            const std::uint64_t rhs = state.gpr[d.rb];
            const std::uint64_t carry_in = (state.xer >> 29U) & 1U;
            const std::uint64_t partial = lhs + rhs;
            const bool carry1 = partial < lhs;
            result = partial + carry_in;
            const bool carry2 = result < partial;
            constexpr std::uint64_t xer_ca = std::uint64_t{1} << 29U;
            state.xer = (carry1 || carry2) ? (state.xer | xer_ca) : (state.xer & ~xer_ca);
            destination = d.rt;
            writes_result = true;
            break;
        }
        case 202: { // addze (OE=0 only): RT = RA + XER[CA]
            if ((instruction & (1U << 10U)) != 0) return StepResult::UnsupportedInstruction;
            const std::uint64_t lhs = state.gpr[d.ra];
            const std::uint64_t carry_in = (state.xer >> 29U) & 1U;
            result = lhs + carry_in;
            constexpr std::uint64_t xer_ca = std::uint64_t{1} << 29U;
            state.xer = (carry_in != 0 && result == 0)
                ? (state.xer | xer_ca) : (state.xer & ~xer_ca);
            destination = d.rt;
            writes_result = true;
            break;
        }
        case 200: { // subfze (OE=0 only): RT = ~RA + XER[CA]
            if ((instruction & (1U << 10U)) != 0) return StepResult::UnsupportedInstruction;
            const std::uint64_t lhs = ~state.gpr[d.ra];
            const std::uint64_t carry_in = (state.xer >> 29U) & 1U;
            result = lhs + carry_in;
            constexpr std::uint64_t xer_ca = std::uint64_t{1} << 29U;
            state.xer = (carry_in != 0 && result == 0)
                ? (state.xer | xer_ca) : (state.xer & ~xer_ca);
            destination = d.rt;
            writes_result = true;
            break;
        }
        case 234: case 232: { // addme / subfme (OE=0 only): lhs + XER[CA] - 1
            if ((instruction & (1U << 10U)) != 0) return StepResult::UnsupportedInstruction;
            const std::uint64_t lhs = d.xo == 234 ? state.gpr[d.ra] : ~state.gpr[d.ra];
            const std::uint64_t carry_in = (state.xer >> 29U) & 1U;
            const std::uint64_t partial = lhs + UINT64_MAX;
            const bool carry1 = partial < lhs;
            result = partial + carry_in;
            const bool carry2 = result < partial;
            constexpr std::uint64_t xer_ca = std::uint64_t{1} << 29U;
            state.xer = (carry1 || carry2) ? (state.xer | xer_ca) : (state.xer & ~xer_ca);
            destination = d.rt;
            writes_result = true;
            break;
        }
        case 266: // add (OE=0 only)
            if ((instruction & (1U << 10U)) != 0) return StepResult::UnsupportedInstruction;
            result = state.gpr[d.ra] + state.gpr[d.rb];
            destination = d.rt;
            writes_result = true;
            break;
        case 40: // subf (OE=0 only)
            if ((instruction & (1U << 10U)) != 0) return StepResult::UnsupportedInstruction;
            result = state.gpr[d.rb] - state.gpr[d.ra];
            destination = d.rt;
            writes_result = true;
            break;
        case 316: // xor
            result = state.gpr[d.rt] ^ state.gpr[d.rb];
            destination = d.ra;
            writes_result = true;
            break;
        case 444: // or
            result = state.gpr[d.rt] | state.gpr[d.rb];
            destination = d.ra;
            writes_result = true;
            break;
        default:
            return StepResult::UnsupportedInstruction;
        }
        break;
    default:
        return StepResult::UnsupportedInstruction;
    }

    if (writes_result) {
        state.gpr[destination] = result;

        if (d.record) {
            update_cr0(state, result, record_32bit ? (std::uint64_t{1} << 31U)
                                                   : (std::uint64_t{1} << 63U));
        }
    }

    if (update_base) state.gpr[d.ra] = updated_address;
    state.pc = branch_taken ? branch_target : state.pc + 4U;
    return StepResult::Executed;
}

} // namespace wx360::cpu
