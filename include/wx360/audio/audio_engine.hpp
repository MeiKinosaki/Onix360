#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace wx360::audio {
// Bounded host-side PCM16 stereo queue. This is a staging interface only:
// XMA decoding and native audio-device playback are not implemented.
class AudioEngine {
public:
    explicit AudioEngine(std::size_t max_frames = 16384) : max_frames_(max_frames) {}

    // Appends complete interleaved stereo frames atomically. On overflow or
    // malformed input, the existing queue remains unchanged.
    [[nodiscard]] bool submit_pcm16(const std::vector<std::int16_t>& samples) {
        if ((samples.size() % 2U) != 0U) return false;
        const std::size_t incoming_frames = samples.size() / 2U;
        if (incoming_frames > max_frames_ - frames()) return false;
        pending_.insert(pending_.end(), samples.begin(), samples.end());
        return true;
    }

    void consume_frames(std::size_t count) noexcept {
        const std::size_t available = frames();
        const std::size_t consumed = std::min(count, available);
        const std::size_t samples = consumed * 2U;
        pending_.erase(pending_.begin(),
                       pending_.begin() + static_cast<std::ptrdiff_t>(samples));
    }
    void reset() noexcept { pending_.clear(); }
    [[nodiscard]] const std::vector<std::int16_t>& pending() const noexcept { return pending_; }
    [[nodiscard]] std::size_t frames() const noexcept { return pending_.size() / 2U; }
    [[nodiscard]] std::size_t capacity_frames() const noexcept { return max_frames_; }
private:
    std::size_t max_frames_;
    std::vector<std::int16_t> pending_;
};
}