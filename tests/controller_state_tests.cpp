#include "onix360/input/controller_state.hpp"
#include "onix360/input/xinput_backend.hpp"
#include <cmath>
#include <cstdlib>
int main() {
    using onix360::input::ControllerState;
    ControllerState pad{};
    pad.buttons = static_cast<std::uint16_t>(onix360::input::A | onix360::input::DpadUp);
    pad.left_x = 1200;
    pad.left_trigger = 200;
    if (!pad.pressed(onix360::input::A) || !pad.pressed(onix360::input::DpadUp)) return EXIT_FAILURE;
    if (pad.pressed(onix360::input::B) || pad.pressed(0)) return EXIT_FAILURE;
    if (pad.left_x != 1200 || pad.left_trigger != 200) return EXIT_FAILURE;
    if (ControllerState::normalize_axis(0) != 0.0F ||
        ControllerState::normalize_axis(7000) != 0.0F ||
        std::fabs(ControllerState::normalize_axis(-32768) + 1.0F) > 0.0001F ||
        std::fabs(ControllerState::normalize_axis(32767) - 1.0F) > 0.0001F)
        return EXIT_FAILURE;
    if (std::fabs(pad.left_x_normalized()) > 0.0001F) return EXIT_FAILURE;
    pad.left_x = 32767;
    if (std::fabs(pad.left_x_normalized() - 1.0F) > 0.0001F) return EXIT_FAILURE;
#ifndef _WIN32
    onix360::input::XInputBackend backend;
    if (backend.poll(pad)) return EXIT_FAILURE;
#endif
    return EXIT_SUCCESS;
}