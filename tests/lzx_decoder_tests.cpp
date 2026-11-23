#include "wx360/compression/lzx_decoder.hpp"
#include <array>
#include <cstdlib>
#include <iostream>
#include <vector>

int main() {
    // Empty/truncated data must be rejected consistently for every regular
    // LZX window size. This is a negative robustness test, not a positive
    // compressed-stream conformance vector.
    const std::array<unsigned, 7> windows{15, 16, 17, 18, 19, 20, 21};
    const std::array<std::uint8_t, 1> invalid{0};
    for (const auto bits : windows) {
        std::vector<std::uint8_t> output{0xAA};
        const auto result = wx360::compression::decode_lzx(
            invalid, bits, 0, 32768, output);
        if (result == wx360::compression::LzxError::None || output.size() != 1 ||
            output[0] != 0xAA) {
            std::cerr << "LZX invalid-stream rejection failed for window " << bits << '\n';
            return EXIT_FAILURE;
        }
    }
    if (wx360::compression::decode_lzx(invalid, 14, 0, 32768, *new std::vector<std::uint8_t>) !=
        wx360::compression::LzxError::InvalidWindow) {
        std::cerr << "LZX invalid-window validation failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "LZX window validation and malformed-input tests passed\n";
    return EXIT_SUCCESS;
}
