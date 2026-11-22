#pragma once

#include <array>
#include <cstdint>

namespace wx360::cpu {

// Architectural state for one emulated Xenon hardware thread.
// Values are stored as 64-bit quantities; instruction semantics will define
// when an operation sign-extends, zero-extends, or truncates a result.
struct XenonThreadState {
    std::array<std::uint64_t, 32> gpr{};
    std::uint64_t pc{0};
    std::uint64_t lr{0};
    std::uint64_t ctr{0};
    std::uint64_t xer{0};
    std::uint32_t cr{0};
    std::uint64_t msr{0};
    std::uint32_t fpscr{0};

    void reset(std::uint64_t entry_point = 0) noexcept {
        gpr.fill(0);
        pc = entry_point;
        lr = ctr = xer = msr = 0;
        cr = fpscr = 0;
    }

    [[nodiscard]] std::uint8_t condition_field(unsigned field) const noexcept {
        if (field >= 8) return 0;
        return static_cast<std::uint8_t>((cr >> (28U - field * 4U)) & 0xFU);
    }

    void set_condition_field(unsigned field, std::uint8_t value) noexcept {
        if (field >= 8) return;
        const unsigned shift = 28U - field * 4U;
        const std::uint32_t mask = 0xFU << shift;
        cr = (cr & ~mask) | ((static_cast<std::uint32_t>(value) & 0xFU) << shift);
    }
};

} // namespace wx360::cpu
