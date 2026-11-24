#pragma once
#include <cstdint>

namespace wx360::input {
struct ControllerState {
    std::uint16_t buttons{};
    std::int16_t left_x{}, left_y{}, right_x{}, right_y{};
    std::uint8_t left_trigger{}, right_trigger{};
    [[nodiscard]] bool pressed(std::uint16_t mask) const noexcept {
        return (buttons & mask) == mask;
    }
};
}