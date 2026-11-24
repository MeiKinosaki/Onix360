#pragma once

#include "wx360/cpu/xenon_state.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <span>
#include <limits>
#include <algorithm>

namespace wx360::cpu {

enum class StepResult {
    Executed,
    UnsupportedInstruction,
    MemoryFault,
    AlignmentFault,
};

struct MemoryAccess {
    void* context{};
    bool (*read)(void*, std::uint64_t, std::span<std::uint8_t>) noexcept{};
    bool (*write)(void*, std::uint64_t, std::span<const std::uint8_t>) noexcept{};
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
    std::uint64_t* fault_address = nullptr,
    const MemoryAccess* bus = nullptr) noexcept {
    const auto d = decode(instruction);
    const auto read_memory = [&](std::uint64_t address, std::span<std::uint8_t> output) noexcept {
        if (bus && bus->read) return bus->read(bus->context, address, output);
        if (address > memory.size() || output.size() > memory.size() - static_cast<std::size_t>(address))
            return false;
        for (std::size_t i = 0; i < output.size(); ++i)
            output[i] = memory[static_cast<std::size_t>(address) + i];
        return true;
    };
    const auto write_memory = [&](std::uint64_t address, std::span<const std::uint8_t> input) noexcept {
        bool written = false;
        if (bus && bus->write) {
            written = bus->write(bus->context, address, input);
        } else {
            if (address > memory.size() || input.size() > memory.size() - static_cast<std::size_t>(address))
                return false;
            for (std::size_t i = 0; i < input.size(); ++i)
                memory[static_cast<std::size_t>(address) + i] = input[i];
            written = true;
        }
        if (written && state.reservation_valid && !input.empty()) {
            const std::uint64_t span = static_cast<std::uint64_t>(input.size() - 1U);
            if (span > std::numeric_limits<std::uint64_t>::max() - address) {
                state.reservation_valid = false; // Conservatively invalidate wrapped ranges.
            } else {
                const std::uint64_t first = address & ~std::uint64_t{31};
                const std::uint64_t last = (address + span) & ~std::uint64_t{31};
                const std::uint64_t reserved_granule = state.reservation_address & ~std::uint64_t{31};
                if (first <= reserved_granule && reserved_granule <= last)
                    state.reservation_valid = false;
            }
        }
        return written;
    };
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
        std::array<std::uint8_t, 4> bytes{};
        if (load) {
            if (!read_memory(address, std::span<std::uint8_t>(bytes.data(), width))) {
                if (fault_address) *fault_address = address;
                return StepResult::MemoryFault;
            }
            if (word_access) {
                result = (static_cast<std::uint32_t>(bytes[0]) << 24U) |
                         (static_cast<std::uint32_t>(bytes[1]) << 16U) |
                         (static_cast<std::uint32_t>(bytes[2]) << 8U) | bytes[3];
            } else if (halfword_access) {
                const std::uint16_t value = static_cast<std::uint16_t>(
                    (static_cast<std::uint16_t>(bytes[0]) << 8U) | bytes[1]);
                result = (d.primary_opcode == 42 || d.primary_opcode == 43)
                    ? static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int16_t>(value)))
                    : value;
            } else {
                result = bytes[0];
            }
            writes_result = true;
        } else {
            if (word_access) {
                const std::uint32_t value = static_cast<std::uint32_t>(state.gpr[d.rt]);
                bytes[0] = static_cast<std::uint8_t>(value >> 24U);
                bytes[1] = static_cast<std::uint8_t>(value >> 16U);
                bytes[2] = static_cast<std::uint8_t>(value >> 8U);
                bytes[3] = static_cast<std::uint8_t>(value);
            } else if (halfword_access) {
                const std::uint16_t value = static_cast<std::uint16_t>(state.gpr[d.rt]);
                bytes[0] = static_cast<std::uint8_t>(value >> 8U);
                bytes[1] = static_cast<std::uint8_t>(value);
            } else {
                bytes[0] = static_cast<std::uint8_t>(state.gpr[d.rt]);
            }
            if (!write_memory(address, std::span<const std::uint8_t>(bytes.data(), width))) {
                if (fault_address) *fault_address = address;
                return StepResult::MemoryFault;
            }
        }
        if (update) { update_base = true; updated_address = address; }
        break;
    }
    case 46: case 47: { // lmw/stmw: transfer GPR[RT..31] as big-endian words.
        const std::uint64_t base = d.ra == 0 ? 0U : state.gpr[d.ra];
        const std::uint64_t address = base +
            static_cast<std::uint64_t>(static_cast<std::int64_t>(d.immediate));
        const std::size_t count = static_cast<std::size_t>(32U - d.rt) * 4U;
        if ((address & 3U) != 0U) {
            if (fault_address) *fault_address = address;
            return StepResult::AlignmentFault;
        }
        std::array<std::uint8_t, 128> bytes{};
        if (d.primary_opcode == 46U) {
            // Stage the complete transfer before changing any guest register.
            if (!read_memory(address, std::span<std::uint8_t>(bytes.data(), count))) {
                if (fault_address) *fault_address = address;
                return StepResult::MemoryFault;
            }
            for (unsigned reg = d.rt; reg < 32U; ++reg) {
                const std::size_t offset = static_cast<std::size_t>(reg - d.rt) * 4U;
                state.gpr[reg] = (static_cast<std::uint32_t>(bytes[offset]) << 24U) |
                    (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U) |
                    (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U) |
                    bytes[offset + 3U];
            }
        } else {
            for (unsigned reg = d.rt; reg < 32U; ++reg) {
                const std::size_t offset = static_cast<std::size_t>(reg - d.rt) * 4U;
                const std::uint32_t value = static_cast<std::uint32_t>(state.gpr[reg]);
                bytes[offset] = static_cast<std::uint8_t>(value >> 24U);
                bytes[offset + 1U] = static_cast<std::uint8_t>(value >> 16U);
                bytes[offset + 2U] = static_cast<std::uint8_t>(value >> 8U);
                bytes[offset + 3U] = static_cast<std::uint8_t>(value);
            }
            if (!write_memory(address, std::span<const std::uint8_t>(bytes.data(), count))) {
                if (fault_address) *fault_address = address;
                return StepResult::MemoryFault;
            }
        }
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
    case 19: {
        if (d.xo == 16U) { // bclr: conditional branch to LR.
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
            if (condition_ok && count_ok) {
                branch_target = state.lr & ~std::uint64_t{3};
                branch_taken = true;
            }
            if ((instruction & 1U) != 0) state.lr = state.pc + 4U;
            break;
        }
        // XL-form privileged return from interrupt (RFI), XO=50.
        if (d.xo != 50U) return StepResult::UnsupportedInstruction;
        branch_target = state.srr0;
        state.msr = state.srr1;
        state.clear_exception();
        branch_taken = true;
        break;
    }
        if (d.xo != 50U) return StepResult::UnsupportedInstruction;
        branch_target = state.srr0;
        state.msr = state.srr1;
        state.clear_exception();
        branch_taken = true;
        break;
    }
    case 48: case 49: case 50: case 51: case 52: case 53: case 54: case 55: { // lfs/lfd/stfs/stfd D-form.
        const bool single = d.primary_opcode == 48 || d.primary_opcode == 49 ||
                            d.primary_opcode == 52 || d.primary_opcode == 53;
        const bool load = d.primary_opcode <= 51;
        const bool update = (d.primary_opcode & 1U) != 0;
        if (update && d.ra == 0) return StepResult::UnsupportedInstruction;
        const std::uint64_t address = (d.ra == 0 ? 0 : state.gpr[d.ra]) +
            static_cast<std::uint64_t>(static_cast<std::int64_t>(d.immediate));
        const std::size_t width = single ? 4U : 8U;
        if ((address & (width - 1U)) != 0U) {
            if (fault_address) *fault_address = address;
            return StepResult::AlignmentFault;
        }
        std::array<std::uint8_t, 8> bytes{};
        if (load && !read_memory(address, std::span<std::uint8_t>(bytes.data(), width))) {
            if (fault_address) *fault_address = address;
            return StepResult::MemoryFault;
        }
        std::uint64_t raw = 0;
        if (load) {
            for (std::size_t n = 0; n < width; ++n) raw = (raw << 8U) | bytes[n];
        }
        if (load) {
            if (single) {
                const float f = std::bit_cast<float>(static_cast<std::uint32_t>(raw));
                state.fpr[d.rt] = std::bit_cast<std::uint64_t>(static_cast<double>(f));
            } else {
                state.fpr[d.rt] = raw;
            }
        } else {
            if (single) raw = std::bit_cast<std::uint32_t>(
                static_cast<float>(std::bit_cast<double>(state.fpr[d.rt])));
            else raw = state.fpr[d.rt];
            for (std::size_t n = 0; n < width; ++n)
                bytes[n] = static_cast<std::uint8_t>(raw >> ((width - 1U - n) * 8U));
            if (!write_memory(address, std::span<const std::uint8_t>(bytes.data(), width))) {
                if (fault_address) *fault_address = address;
                return StepResult::MemoryFault;
            }
        }
        if (update) state.gpr[d.ra] = address;
        break;
    }
    case 63: { // Floating-point arithmetic, compare, and conversion subset.
        const double a = std::bit_cast<double>(state.fpr[d.ra]);
        const double b = std::bit_cast<double>(state.fpr[d.rb]);
        const unsigned fp_xo = (instruction >> 1U) & 0x1FU;
        if (d.xo == 0U || d.xo == 32U) { // fcmpu / fcmpo
            const unsigned bf = (instruction >> 23U) & 7U;
            std::uint8_t field = 0x1U;
            if (!std::isnan(a) && !std::isnan(b))
                field = a < b ? 0x8U : (a > b ? 0x4U : 0x2U);
            state.set_condition_field(bf, field);
            if (std::isnan(a) || std::isnan(b)) state.fpscr |= (1U << 3U);
            break;
        }
        double value = 0.0;
        switch (fp_xo) {
        case 12: value = static_cast<double>(static_cast<float>(a)); break; // frsp
        case 18: value = a / b; break; // fdiv
        case 20: value = a - b; break; // fsub
        case 21: value = a + b; break; // fadd
        case 25: { const unsigned frc = (instruction >> 6U) & 0x1FU;
            value = a * std::bit_cast<double>(state.fpr[frc]); break; } // fmul
        case 40: value = -a; break; // fneg
        case 72: value = a; break; // fmr
        case 264: value = std::fabs(a); break; // fabs
        case 136: value = -std::fabs(a); break; // fnabs
        default: return StepResult::UnsupportedInstruction;
        }
        // Sticky FPSCR summary and primary exception classes.
        if (std::isnan(value) && !std::isnan(a) && !std::isnan(b))
            state.fpscr |= 0xA0000000U; // FX | VX
        if (std::isinf(value) && std::isfinite(a) && std::isfinite(b))
            state.fpscr |= 0x90000000U; // FX | OX
        if (value == 0.0 && a != 0.0 && b != 0.0 &&
            std::isfinite(a) && std::isfinite(b))
            state.fpscr |= 0x88000000U; // FX | UX
        if (fp_xo == 18U && b == 0.0 && a != 0.0)
            state.fpscr |= 0x84000000U; // FX | ZX
        state.fpr[d.rt] = std::bit_cast<std::uint64_t>(value);
        break;
    }
    case 4: { // VMX/AltiVec three-register arithmetic subset.
        const unsigned vd = d.rt, va = d.ra, vb = d.rb;
        auto& out = state.vr[vd];
        const auto& a = state.vr[va];
        const auto& b = state.vr[vb];
        // VX-form VMX encodings carry an 11-bit extended opcode including bit 0.
        const unsigned xo = instruction & 0x7FFU;
        if (xo == 0U || xo == 1024U) { // vaddubm / vsububm
            for (unsigned lane = 0; lane < 4; ++lane) {
                std::uint32_t value = 0;
                for (unsigned byte = 0; byte < 4; ++byte) {
                    const unsigned shift = 24U - byte * 8U;
                    const unsigned x = (a.lane[lane] >> shift) & 0xFFU;
                    const unsigned y = (b.lane[lane] >> shift) & 0xFFU;
                    const unsigned z = xo == 0U ? x + y : x - y;
                    value |= (z & 0xFFU) << shift;
                }
                out.lane[lane] = value;
            }
        } else if (xo == 64U || xo == 1088U) { // vadduhm / vsubuhm
            for (unsigned lane = 0; lane < 4; ++lane) {
                const std::uint32_t x = a.lane[lane], y = b.lane[lane];
                const std::uint32_t hi = xo == 64U
                    ? (((x >> 16U) + (y >> 16U)) & 0xFFFFU)
                    : (((x >> 16U) - (y >> 16U)) & 0xFFFFU);
                const std::uint32_t lo = xo == 64U
                    ? (((x & 0xFFFFU) + (y & 0xFFFFU)) & 0xFFFFU)
                    : (((x & 0xFFFFU) - (y & 0xFFFFU)) & 0xFFFFU);
                out.lane[lane] = (hi << 16U) | lo;
            }
        } else if (xo == 128U || xo == 1152U) { // vadduwm / vsubuwm
            for (unsigned lane = 0; lane < 4; ++lane)
                out.lane[lane] = xo == 128U ? a.lane[lane] + b.lane[lane]
                                            : a.lane[lane] - b.lane[lane];
        } else if (xo == 10U || xo == 74U) { // vaddfp / vsubfp
            for (unsigned lane = 0; lane < 4; ++lane) {
                const float x = std::bit_cast<float>(a.lane[lane]);
                const float y = std::bit_cast<float>(b.lane[lane]);
                out.lane[lane] = std::bit_cast<std::uint32_t>(
                    xo == 10U ? x + y : x - y);
            }
        } else if (xo == 1028U || xo == 1092U || xo == 1156U ||
                   xo == 1220U || xo == 1284U) { // vand/vandc/vor/vxor/vnor
            for (unsigned lane = 0; lane < 4; ++lane) {
                if (xo == 1028U) out.lane[lane] = a.lane[lane] & b.lane[lane];
                else if (xo == 1092U) out.lane[lane] = a.lane[lane] & ~b.lane[lane];
                else if (xo == 1156U) out.lane[lane] = a.lane[lane] | b.lane[lane];
                else if (xo == 1220U) out.lane[lane] = a.lane[lane] ^ b.lane[lane];
                else out.lane[lane] = ~(a.lane[lane] | b.lane[lane]);
            }
        } else {
            return StepResult::UnsupportedInstruction;
        }
        break;
    }
    case 31:
        switch (d.xo) {
        case 103: case 359: case 231: case 487: { // LVX/LVXL/STVX/STVXL.
            const bool store_vector = d.xo == 231U || d.xo == 487U;
            const std::uint64_t effective = (d.ra == 0 ? 0U : state.gpr[d.ra]) +
                                            state.gpr[d.rb];
            const std::uint64_t address = effective & ~std::uint64_t{15};
            std::array<std::uint8_t, 16> bytes{};
            if (!store_vector) {
                if (!read_memory(address, bytes)) {
                    if (fault_address) *fault_address = address;
                    return StepResult::MemoryFault;
                }
                for (unsigned lane = 0; lane < 4; ++lane) {
                    const unsigned offset = lane * 4U;
                    state.vr[d.rt].lane[lane] =
                        (static_cast<std::uint32_t>(bytes[offset]) << 24U) |
                        (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U) |
                        (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U) |
                        bytes[offset + 3U];
                }
            } else {
                for (unsigned lane = 0; lane < 4; ++lane) {
                    const std::uint32_t value = state.vr[d.rt].lane[lane];
                    const unsigned offset = lane * 4U;
                    bytes[offset] = static_cast<std::uint8_t>(value >> 24U);
                    bytes[offset + 1U] = static_cast<std::uint8_t>(value >> 16U);
                    bytes[offset + 2U] = static_cast<std::uint8_t>(value >> 8U);
                    bytes[offset + 3U] = static_cast<std::uint8_t>(value);
                }
                if (!write_memory(address, bytes)) {
                    if (fault_address) *fault_address = address;
                    return StepResult::MemoryFault;
                }
            }
            break;
        }
        case 20: { // lwarx: load word and establish a reservation.
            const std::uint64_t address = (d.ra == 0 ? 0U : state.gpr[d.ra]) +
                                          state.gpr[d.rb];
            if ((address & 3U) != 0U) {
                if (fault_address) *fault_address = address;
                return StepResult::AlignmentFault;
            }
            std::array<std::uint8_t, 4> bytes{};
            if (!read_memory(address, bytes)) {
                if (fault_address) *fault_address = address;
                return StepResult::MemoryFault;
            }
            result = (static_cast<std::uint32_t>(bytes[0]) << 24U) |
                     (static_cast<std::uint32_t>(bytes[1]) << 16U) |
                     (static_cast<std::uint32_t>(bytes[2]) << 8U) | bytes[3];
            writes_result = true;
            state.reservation_valid = true;
            state.reservation_address = address;
            break;
        }
        case 150: { // stwcx.: conditional word store; Rc must be set.
            if (!d.record) return StepResult::UnsupportedInstruction;
            const std::uint64_t address = (d.ra == 0 ? 0U : state.gpr[d.ra]) +
                                          state.gpr[d.rb];
            if ((address & 3U) != 0U) {
                if (fault_address) *fault_address = address;
                return StepResult::AlignmentFault;
            }
            const bool success = state.reservation_valid &&
                state.reservation_address == address;
            state.reservation_valid = false;
            if (success) {
                const std::uint32_t value = static_cast<std::uint32_t>(state.gpr[d.rt]);
                const std::array<std::uint8_t, 4> bytes{
                    static_cast<std::uint8_t>(value >> 24U),
                    static_cast<std::uint8_t>(value >> 16U),
                    static_cast<std::uint8_t>(value >> 8U),
                    static_cast<std::uint8_t>(value)};
                if (!write_memory(address, bytes)) {
                    if (fault_address) *fault_address = address;
                    return StepResult::MemoryFault;
                }
            }
            state.set_condition_field(0, static_cast<std::uint8_t>(
                (success ? 0x2U : 0U) | ((state.xer >> 31U) & 1U)));
            break;
        }
        case 23: case 55: case 87: case 119:
        case 279: case 311: case 343: case 375:
        case 151: case 183: case 215: case 247:
        case 407: case 439: case 534: case 662:
        case 790: case 918: { // Indexed integer loads/stores.
            const bool store = d.xo == 151 || d.xo == 183 || d.xo == 215 ||
                               d.xo == 247 || d.xo == 407 || d.xo == 439 || d.xo == 662 || d.xo == 918;
            const bool byte_access = d.xo == 87 || d.xo == 119 ||
                                     d.xo == 215 || d.xo == 247;
            const bool half_access = d.xo == 279 || d.xo == 311 ||
                                     d.xo == 343 || d.xo == 375 || d.xo == 790 ||
                                     d.xo == 407 || d.xo == 439 || d.xo == 918;
            const bool reversed = d.xo == 534 || d.xo == 662 ||
                                  d.xo == 790 || d.xo == 918;
            const bool update = d.xo == 55 || d.xo == 119 || d.xo == 183 ||
                                d.xo == 247 || d.xo == 311 || d.xo == 375 ||
                                d.xo == 439;
            if (update && (d.ra == 0 || (!store && d.ra == d.rt)))
                return StepResult::UnsupportedInstruction;
            const std::size_t width = byte_access ? 1U : (half_access ? 2U : 4U);
            const std::uint64_t address = (d.ra == 0 ? 0U : state.gpr[d.ra]) +
                                          state.gpr[d.rb];
            if (width > 1U && (address & (width - 1U)) != 0U) {
                if (fault_address) *fault_address = address;
                return StepResult::AlignmentFault;
            }
            std::array<std::uint8_t, 4> bytes{};
            if (!store) {
                if (!read_memory(address, std::span<std::uint8_t>(bytes.data(), width))) {
                    if (fault_address) *fault_address = address;
                    return StepResult::MemoryFault;
                }
                if (reversed) std::reverse(bytes.begin(), bytes.begin() + width);
                if (width == 4U) {
                    result = (static_cast<std::uint32_t>(bytes[0]) << 24U) |
                             (static_cast<std::uint32_t>(bytes[1]) << 16U) |
                             (static_cast<std::uint32_t>(bytes[2]) << 8U) | bytes[3];
                } else if (width == 2U) {
                    const std::uint16_t value = static_cast<std::uint16_t>(
                        (static_cast<std::uint16_t>(bytes[0]) << 8U) | bytes[1]);
                    result = (d.xo == 343 || d.xo == 375)
                        ? static_cast<std::uint64_t>(static_cast<std::int64_t>(
                              static_cast<std::int16_t>(value))) : value;
                } else {
                    result = bytes[0];
                }
                writes_result = true;
            } else {
                const std::uint32_t value = static_cast<std::uint32_t>(state.gpr[d.rt]);
                for (std::size_t n = 0; n < width; ++n)
                    bytes[n] = static_cast<std::uint8_t>(value >> ((width - 1U - n) * 8U));
                if (reversed) std::reverse(bytes.begin(), bytes.begin() + width);
                if (!write_memory(address, std::span<const std::uint8_t>(bytes.data(), width))) {
                    if (fault_address) *fault_address = address;
                    return StepResult::MemoryFault;
                }
            }
            if (update) state.gpr[d.ra] = address;
            break;
        }
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
            const unsigned spr = (((instruction >> 11U) & 0x1FU) << 5U) |
                                 ((instruction >> 16U) & 0x1FU);
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
            const unsigned spr = (((instruction >> 11U) & 0x1FU) << 5U) |
                                 ((instruction >> 16U) & 0x1FU);
            const std::uint64_t source = state.gpr[d.rt];
            switch (spr) {
            case 1: state.xer = source; break; // XER
            case 8: state.lr = source; break;  // LR
            case 9: state.ctr = source; break; // CTR
            default: return StepResult::UnsupportedInstruction;
            }
            break;
        }
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
