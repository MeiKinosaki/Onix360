#pragma once

#include <cstdint>
#include <span>

namespace onix360::audio {

// Windows Multimedia (waveOut) PCM16 stereo device output.
// This is a host playback backend, not an Xbox 360 XMA decoder.
class WindowsPcmOutput {
public:
    // Plays one interleaved stereo PCM16 block. Returns false on invalid
    // input, unavailable device, or playback failure. Blocks are bounded
    // to the WinMM DWORD buffer-size limit.
    [[nodiscard]] bool play(std::span<const std::int16_t> samples,
                            std::uint32_t sample_rate = 48000U) noexcept;
    [[nodiscard]] std::uint32_t last_error() const noexcept { return last_error_; }

private:
    std::uint32_t last_error_{};
};

} // namespace onix360::audio
