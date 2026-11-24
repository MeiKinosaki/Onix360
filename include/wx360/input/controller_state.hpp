#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdlib>

namespace wx360::input {
enum ControllerButton : std::uint16_t {
    DpadUp = 1U << 0U, DpadDown = 1U << 1U,
    DpadLeft = 1U << 2U, DpadRight = 1U << 3U,
    Start = 1U << 4U, Back = 1U << 5U,
    LeftStick = 1U << 6U, RightStick = 1U << 7U,
    LeftShoulder = 1U << 8U, RightShoulder = 1U << 9U,
    A = 1U << 12U, B = 1U << 13U, X = 1U << 14U, Y = 1U << 15U
};

struct ControllerState {
    std::uint16_t buttons{};
    std::int16_t left_x{}, left_y{}, right_x{}, right_y{};
    std::uint8_t left_trigger{}, right_trigger{};

    [[nodiscard]] bool pressed(std::uint16_t mask) const noexcept {
        return mask != 0U && (buttons & mask) == mask;
    }

    // Convert signed XInput-style axes to [-1, 1], applying a radial
    // per-axis deadzone. Raw values remain available to callers.
    [[nodiscard]] static float normalize_axis(std::int16_t value,
                                               std::int16_t deadzone = 7849) noexcept {
        const int bounded_deadzone = std::clamp(static_cast<int>(deadzone), 0, 32767);
        const int raw = static_cast<int>(value);
        const int magnitude = std::abs(raw);
        if (magnitude <= bounded_deadzone) return 0.0F;
        const float range = 32767.0F - static_cast<float>(bounded_deadzone);
        const float scaled = std::clamp(
            static_cast<float>(magnitude - bounded_deadzone) / range, 0.0F, 1.0F);
        return raw < 0 ? -scaled : scaled;
    }

    [[nodiscard]] float left_x_normalized(std::int16_t deadzone = 7849) const noexcept {
        return normalize_axis(left_x, deadzone);
    }
    [[nodiscard]] float left_y_normalized(std::int16_t deadzone = 7849) const noexcept {
        return normalize_axis(left_y, deadzone);
    }
    [[nodiscard]] float right_x_normalized(std::int16_t deadzone = 8689) const noexcept {
        return normalize_axis(right_x, deadzone);
    }
    [[nodiscard]] float right_y_normalized(std::int16_t deadzone = 8689) const noexcept {
        return normalize_axis(right_y, deadzone);
    }
};
}