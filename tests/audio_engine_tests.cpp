#include "wx360/audio/audio_engine.hpp"
#include <cstdlib>
#include <vector>
int main() {
    wx360::audio::AudioEngine audio(4);
    if (!audio.submit_pcm16({1, -1, 2, -2})) return EXIT_FAILURE;
    if (audio.frames() != 2 || audio.pending()[1] != -1) return EXIT_FAILURE;
    if (audio.submit_pcm16({1, 2, 3})) return EXIT_FAILURE;
    if (!audio.submit_pcm16({1, 2, 3, 4, 5, 6, 7, 8})) return EXIT_FAILURE;
    if (audio.submit_pcm16({1,2,3,4,5,6,7,8,9,10})) return EXIT_FAILURE;
    audio.reset();
    return audio.frames() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}