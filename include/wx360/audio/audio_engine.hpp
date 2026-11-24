#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace wx360::audio {
// Host-side PCM16 staging buffer. Device playback and XMA decoding are not implemented.
class AudioEngine {
public:
    explicit AudioEngine(std::size_t max_frames = 16384) : max_frames_(max_frames) {}
    [[nodiscard]] bool submit_pcm16(std::vector<std::int16_t> interleaved_stereo) {
        if (interleaved_stereo.size() % 2U != 0U ||
            interleaved_stereo.size() / 2U > max_frames_) return false;
        pending_.swap(interleaved_stereo);
        return true;
    }
    void reset() noexcept { pending_.clear(); }
    [[nodiscard]] const std::vector<std::int16_t>& pending() const noexcept { return pending_; }
    [[nodiscard]] std::size_t frames() const noexcept { return pending_.size() / 2U; }
private:
    std::size_t max_frames_;
    std::vector<std::int16_t> pending_;
};
}