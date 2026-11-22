#pragma once

#include "wx360/cpu/xenon_state.hpp"

#include <cstdint>

namespace wx360::cpu {

enum class StepResult {
    Executed,
    UnsupportedInstruction,
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
    XenonThreadState& state, std::uint32_t instruction) noexcept {
    const auto d = decode(instruction);
    std::uint64_t result = 0;
    bool writes_result = false;
    std::uint8_t destination = d.rt;

    switch (d.primary_opcode) {
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
    case 31:
        switch (d.xo) {
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
            constexpr std::uint64_t sign_bit = std::uint64_t{1} << 63U;
            std::uint8_t cr0 = 0;
            if ((result & sign_bit) != 0) cr0 |= 0x8U; // LT
            else if (result != 0) cr0 |= 0x4U;          // GT
            else cr0 |= 0x2U;                           // EQ
            if ((state.xer & (std::uint64_t{1} << 31U)) != 0) cr0 |= 0x1U; // SO
            state.set_condition_field(0, cr0);
        }
    }

    state.pc += 4;
    return StepResult::Executed;
}

} // namespace wx360::cpu
