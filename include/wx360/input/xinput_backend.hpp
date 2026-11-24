#pragma once

#include "wx360/input/controller_state.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <Xinput.h>
#endif

namespace wx360::input {

// Polls the first XInput controller. Returns false when unavailable.
// On non-Windows hosts this backend is deliberately inert so CI remains portable.
class XInputBackend {
public:
    [[nodiscard]] bool poll(ControllerState& output, unsigned user_index = 0) const noexcept {
#ifdef _WIN32
        if (user_index >= XUSER_MAX_COUNT) return false;
        XINPUT_STATE native_state{};
        if (XInputGetState(user_index, &native_state) != ERROR_SUCCESS) {
            output = {};
            return false;
        }

        const auto& pad = native_state.Gamepad;
        ControllerState next{};
        next.buttons = static_cast<std::uint16_t>(pad.wButtons);
        next.left_x = pad.sThumbLX;
        next.left_y = pad.sThumbLY;
        next.right_x = pad.sThumbRX;
        next.right_y = pad.sThumbRY;
        next.left_trigger = pad.bLeftTrigger;
        next.right_trigger = pad.bRightTrigger;
        output = next;
        return true;
#else
        (void)output;
        (void)user_index;
        return false;
#endif
    }
};

} // namespace wx360::input
