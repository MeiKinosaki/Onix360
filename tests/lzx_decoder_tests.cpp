#include "wx360/compression/lzx_decoder.hpp"
#include <array>
#include <climits>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#ifndef WX360_TEST_DATA_DIR
#error WX360_TEST_DATA_DIR must point to generated LZX reference vectors
#endif

int main() {
    const std::string data_dir = WX360_TEST_DATA_DIR;
    std::ifstream compressed_file(data_dir + "/lzx_reference.bin", std::ios::binary);
    std::ifstream expected_file(data_dir + "/lzx_reference.expected", std::ios::binary);
    if (!compressed_file || !expected_file) {
        std::cerr << "LZX independent reference vector is missing\n";
        return EXIT_FAILURE;
    }
    const std::vector<std::uint8_t> compressed{
        std::istreambuf_iterator<char>(compressed_file), std::istreambuf_iterator<char>()};
    const std::string expected{
        std::istreambuf_iterator<char>(expected_file), std::istreambuf_iterator<char>()};
    std::vector<std::uint8_t> decoded;
    const auto positive = wx360::compression::decode_lzx(
        compressed, 15, 0, expected.size(), decoded);
    if (positive != wx360::compression::LzxError::None ||
        decoded != std::vector<std::uint8_t>(expected.begin(), expected.end())) {
        std::cerr << "LZX positive reference-vector decode failed\n";
        return EXIT_FAILURE;
    }

    const std::array<unsigned, 7> windows{15, 16, 17, 18, 19, 20, 21};
    const std::array<std::uint8_t, 1> truncated{0};
    for (const auto bits : windows) {
        std::vector<std::uint8_t> output{0xAA, 0x55};
        const auto result = wx360::compression::decode_lzx(
            truncated, bits, 0, 32768, output);
        if (result == wx360::compression::LzxError::None ||
            output != std::vector<std::uint8_t>{0xAA, 0x55}) {
            std::cerr << "LZX truncated-stream rejection failed for window "
                      << bits << '\n';
            return EXIT_FAILURE;
        }
    }

    std::vector<std::uint8_t> output{0x11, 0x22};
    if (wx360::compression::decode_lzx(truncated, 14, 0, 32768, output) !=
        wx360::compression::LzxError::InvalidWindow ||
        output != std::vector<std::uint8_t>{0x11, 0x22}) {
        std::cerr << "LZX invalid-window validation failed\n";
        return EXIT_FAILURE;
    }
    if (wx360::compression::decode_lzx(truncated, 15, 0, 0, output) !=
        wx360::compression::LzxError::InvalidOutputSize ||
        output != std::vector<std::uint8_t>{0x11, 0x22}) {
        std::cerr << "LZX zero-output guard failed\n";
        return EXIT_FAILURE;
    }
    if (wx360::compression::decode_lzx(
            truncated, 15, static_cast<unsigned>(INT_MAX) + 1u, 32768, output) !=
        wx360::compression::LzxError::InvalidOutputSize ||
        output != std::vector<std::uint8_t>{0x11, 0x22}) {
        std::cerr << "LZX reset-interval overflow guard failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "LZX independent positive vector and negative guards passed\n";
    return EXIT_SUCCESS;
}
