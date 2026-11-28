#include "onix360/audio/windows_pcm_output.hpp"

#include <array>
#include <cstdlib>
#include <cstdint>

int main() {
    onix360::audio::WindowsPcmOutput output;
    constexpr std::array<std::int16_t, 4> stereo{100, -100, 200, -200};
    if (output.play({}, 48000U)) return EXIT_FAILURE;
    if (output.last_error() == 0U) return EXIT_FAILURE;
    if (output.play(stereo, 0U)) return EXIT_FAILURE;
    if (output.last_error() == 0U) return EXIT_FAILURE;
    if (output.play(stereo, 384000U)) return EXIT_FAILURE;
    if (output.last_error() == 0U) return EXIT_FAILURE;
    return EXIT_SUCCESS;
}
