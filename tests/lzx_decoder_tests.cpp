#include "wx360/compression/lzx_decoder.hpp"
#include <array>
#include <climits>
#include <cstdlib>
#include <iostream>
#include <vector>

int main() {
    // Malformed/truncated inputs must fail without modifying caller output.
    const std::array<unsigned, 7> windows{15, 16, 17, 18, 19, 20, 21};
    const std::array<std::uint8_t, 1> truncated{0};
    for (const auto bits : windows) {
        std::vector<std::uint8_t> output{0xAA, 0x55};
        const auto result = wx360::compression::decode_lzx(
            truncated, bits, 0, 32768, output);
        if (result == wx360::compression::LzxError::None ||
            output != std::vector<std::uint8_t>{0xAA, 0x55}) {
            std::cerr << "LZX truncated-stream rejection failed for window "
                      << bits << '\\n';
            return EXIT_FAILURE;
        }
    }

    std::vector<std::uint8_t> output{0x11, 0x22};
    if (wx360::compression::decode_lzx(truncated, 14, 0, 32768, output) !=
        wx360::compression::LzxError::InvalidWindow ||
        output != std::vector<std::uint8_t>{0x11, 0x22}) {
        std::cerr << "LZX invalid-window validation failed\\n";
        return EXIT_FAILURE;
    }
    if (wx360::compression::decode_lzx(truncated, 15, 0, 0, output) !=
        wx360::compression::LzxError::InvalidOutputSize ||
        output != std::vector<std::uint8_t>{0x11, 0x22}) {
        std::cerr << "LZX zero-output guard failed\\n";
        return EXIT_FAILURE;
    }
    if (wx360::compression::decode_lzx(
            truncated, 15, static_cast<unsigned>(INT_MAX) + 1u, 32768, output) !=
        wx360::compression::LzxError::InvalidOutputSize ||
        output != std::vector<std::uint8_t>{0x11, 0x22}) {
        std::cerr << "LZX reset-interval overflow guard failed\\n";
        return EXIT_FAILURE;
    }

    std::cout << "LZX malformed-input and parameter-guard tests passed\\n";
    return EXIT_SUCCESS;
}
