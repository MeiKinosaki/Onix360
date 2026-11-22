#pragma once

#include "wx360/cpu/xenon_state.hpp"

#include <cstdint>
#include <span>

namespace wx360::cpu {

enum class StepResult {
    Executed,
    UnsupportedInstruction,
    MemoryFault,
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
    std::span<std::uint8_t> memory = {}) noexcept {
    const auto d = decode(instruction);
    std::uint64_t result = 0;
    bool writes_result = false;
    bool record_32bit = false;
    std::uint8_t destination = d.rt;
    bool branch_taken = false;
    std::uint64_t branch_target = 0;

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
    case 32: case 34: case 36: case 38: { // lwz, lbz, stw, stb
        const std::int64_t displacement = d.immediate;
        const std::uint64_t base = d.ra == 0 ? 0 : state.gpr[d.ra];
        const std::uint64_t address = static_cast<std::uint64_t>(static_cast<std::int64_t>(base) + displacement);
        const bool word_access = d.primary_opcode == 32 || d.primary_opcode == 36;
        const std::size_t width = word_access ? 4U : 1U;
        if (address > memory.size() || width > memory.size() - static_cast<std::size_t>(address))
            return StepResult::MemoryFault;
        const auto offset = static_cast<std::size_t>(address);
        if (d.primary_opcode == 32) {
            result = (static_cast<std::uint32_t>(memory[offset]) << 24U) |
                     (static_cast<std::uint32_t>(memory[offset + 1]) << 16U) |
                     (static_cast<std::uint32_t>(memory[offset + 2]) << 8U) |
                     static_cast<std::uint32_t>(memory[offset + 3]);
            writes_result = true;
        } else if (d.primary_opcode == 34) {
            result = memory[offset];
            writes_result = true;
        } else if (d.primary_opcode == 36) {
            const std::uint32_t value = static_cast<std::uint32_t>(state.gpr[d.rt]);
            memory[offset] = static_cast<std::uint8_t>(value >> 24U);
            memory[offset + 1] = static_cast<std::uint8_t>(value >> 16U);
            memory[offset + 2] = static_cast<std::uint8_t>(value >> 8U);
            memory[offset + 3] = static_cast<std::uint8_t>(value);
        } else {
            memory[offset] = static_cast<std::uint8_t>(state.gpr[d.rt]);
        }
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
        case 28:  // and
            result = state.gpr[d.rt] & state.gpr[d.rb];
            destination = d.ra;
            writes_result = true;
            break;
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

    state.pc = branch_taken ? branch_target : state.pc + 4U;
    return StepResult::Executed;
}

} // namespace wx360::cpu
