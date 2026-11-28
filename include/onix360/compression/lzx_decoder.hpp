#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace onix360::compression {
enum class LzxError : std::uint8_t {
    None, InvalidWindow, InvalidOutputSize, DecoderInitFailed, DecodeFailed
};

// Decodes one regular (non-DELTA) LZX stream. The caller supplies the exact
// output size and the reset interval required by the enclosing container.
[[nodiscard]] LzxError decode_lzx(
    std::span<const std::uint8_t> input,
    unsigned window_bits,
    unsigned reset_interval,
    std::size_t output_size,
    std::vector<std::uint8_t>& output);
} // namespace onix360::compression
