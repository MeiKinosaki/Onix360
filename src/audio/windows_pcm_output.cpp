#include "wx360/audio/windows_pcm_output.hpp"

#if defined(_WIN32)
#include <windows.h>
#include <mmsystem.h>

#include <limits>

namespace wx360::audio {

bool WindowsPcmOutput::play(std::span<const std::int16_t> samples,
                            std::uint32_t sample_rate) noexcept {
    last_error_ = 0;
    constexpr std::uint32_t min_rate = 8000U;
    constexpr std::uint32_t max_rate = 192000U;
    constexpr std::size_t bytes_per_frame = 4U; // stereo * 16-bit
    if (samples.empty() || (samples.size() % 2U) != 0U ||
        sample_rate < min_rate || sample_rate > max_rate ||
        samples.size() > static_cast<std::size_t>(
            std::numeric_limits<DWORD>::max()) / sizeof(std::int16_t)) {
        last_error_ = ERROR_INVALID_PARAMETER;
        return false;
    }

    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 2;
    format.nSamplesPerSec = sample_rate;
    format.wBitsPerSample = 16;
    format.nBlockAlign = static_cast<WORD>(bytes_per_frame);
    format.nAvgBytesPerSec = sample_rate * static_cast<DWORD>(bytes_per_frame);
    format.cbSize = 0;

    HWAVEOUT device{};
    MMRESULT result = waveOutOpen(&device, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL);
    if (result != MMSYSERR_NOERROR) {
        last_error_ = static_cast<std::uint32_t>(result);
        return false;
    }

    WAVEHDR header{};
    header.lpData = reinterpret_cast<LPSTR>(
        const_cast<std::int16_t*>(samples.data()));
    header.dwBufferLength = static_cast<DWORD>(
        samples.size() * sizeof(std::int16_t));

    result = waveOutPrepareHeader(device, &header, sizeof(header));
    if (result == MMSYSERR_NOERROR) {
        result = waveOutWrite(device, &header, sizeof(header));
        if (result == MMSYSERR_NOERROR) {
            const ULONGLONG deadline = GetTickCount64() + 10000ULL;
            while ((header.dwFlags & WHDR_DONE) == 0U &&
                   GetTickCount64() < deadline) {
                Sleep(1);
            }
            if ((header.dwFlags & WHDR_DONE) == 0U) {
                (void)waveOutReset(device);
                last_error_ = WAIT_TIMEOUT;
            }
        }
        const MMRESULT unprepare = waveOutUnprepareHeader(device, &header, sizeof(header));
        if (result == MMSYSERR_NOERROR && unprepare != MMSYSERR_NOERROR)
            result = unprepare;
    }

    if (result != MMSYSERR_NOERROR && last_error_ == 0U)
        last_error_ = static_cast<std::uint32_t>(result);
    const MMRESULT close_result = waveOutClose(device);
    if (result == MMSYSERR_NOERROR && close_result != MMSYSERR_NOERROR) {
        last_error_ = static_cast<std::uint32_t>(close_result);
        return false;
    }
    return last_error_ == 0U && result == MMSYSERR_NOERROR;
}

} // namespace wx360::audio
#else
namespace wx360::audio {
bool WindowsPcmOutput::play(std::span<const std::int16_t>,
                            std::uint32_t) noexcept {
    last_error_ = 1U;
    return false;
}
} // namespace wx360::audio
#endif
