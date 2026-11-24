#include "wx360/audio/audio_engine.hpp"
#include <cstdlib>
#include <vector>
int main() {
    wx360::audio::AudioEngine audio(4);
    if (!audio.submit_pcm16({1, -1, 2, -2})) return EXIT_FAILURE;
    if (audio.frames() != 2 || audio.pending()[1] != -1) return EXIT_FAILURE;
    if (audio.submit_pcm16({1, 2, 3})) return EXIT_FAILURE; // odd sample count
    if (audio.submit_pcm16({1, 2, 3, 4, 5, 6})) return EXIT_FAILURE; // capacity overflow
    if (audio.frames() != 2) return EXIT_FAILURE; // rejected input is atomic
    if (!audio.submit_pcm16({3, -3, 4, -4})) return EXIT_FAILURE;
    if (audio.frames() != 4 || audio.pending()[4] != 3) return EXIT_FAILURE;
    if (audio.submit_pcm16({5, -5})) return EXIT_FAILURE;
    audio.consume_frames(2);
    if (audio.frames() != 2 || audio.pending()[0] != 3) return EXIT_FAILURE;
    audio.consume_frames(99);
    if (audio.frames() != 0) return EXIT_FAILURE;
    audio.reset();
    return audio.frames() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}