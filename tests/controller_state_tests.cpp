#include "wx360/input/controller_state.hpp"
#include <cstdlib>
int main() {
    wx360::input::ControllerState pad{};
    pad.buttons = 0x0005U;
    pad.left_x = 1200;
    pad.left_trigger = 200;
    if (!pad.pressed(0x0001U) || !pad.pressed(0x0005U)) return EXIT_FAILURE;
    if (pad.pressed(0x0002U) || pad.left_x != 1200 || pad.left_trigger != 200)
        return EXIT_FAILURE;
    return EXIT_SUCCESS;
}