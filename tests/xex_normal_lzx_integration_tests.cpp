#include "wx360/xex/xex_image_loader.hpp"
#include "wx360/crypto/sha1.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#ifndef WX360_TEST_DATA_DIR
#error WX360_TEST_DATA_DIR must point to generated LZX reference vectors
#endif

static void be32(std::vector<std::uint8_t>& b, std::size_t p, std::uint32_t v) {
    b[p] = static_cast<std::uint8_t>(v >> 24);
    b[p + 1] = static_cast<std::uint8_t>(v >> 16);
    b[p + 2] = static_cast<std::uint8_t>(v >> 8);
    b[p + 3] = static_cast<std::uint8_t>(v);
}

int main() {
    using namespace wx360;
    using namespace wx360::xex;

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
    if (compressed.empty() || compressed.size() > 0xFFFFu || expected.empty()) {
        std::cerr << "LZX independent reference vector has unsupported test size\n";
        return EXIT_FAILURE;
    }

    // Wrap the independently encoded CAB-compatible LZX stream in one
    // SHA-1-authenticated XEX Normal block with a zero next-block descriptor.
    std::vector<std::uint8_t> block(24 + 2 + compressed.size() + 2, 0);
    block[24] = static_cast<std::uint8_t>(compressed.size() >> 8);
    block[25] = static_cast<std::uint8_t>(compressed.size());
    std::copy(compressed.begin(), compressed.end(), block.begin() + 26);
    NormalBlockDescriptor first{};
    first.block_size = static_cast<std::uint32_t>(block.size());
    first.hash = crypto::sha1(block);

    FileFormatInfo format{};
    format.compression = XexCompression::Normal;
    format.normal_window_size = 0x8000;
    format.image_size = static_cast<std::uint32_t>(expected.size());
    format.first_normal_block = first;

    std::vector<std::uint8_t> output{0x5A};
    if (XexImageLoader::decompress_normal_lzx(block, format, output) !=
            NormalDecompressError::Ok ||
        output != std::vector<std::uint8_t>(expected.begin(), expected.end())) {
        std::cerr << "XEX Normal positive LZX reference-vector integration failed\n";
        return EXIT_FAILURE;
    }

    // Keep the negative path: a correctly framed/hash-authenticated block
    // whose LZX payload is malformed must be rejected without changing output.
    std::vector<std::uint8_t> invalid_block(24 + 2 + 1 + 2, 0);
    invalid_block[24] = 0; invalid_block[25] = 1;
    invalid_block[26] = 0;
    NormalBlockDescriptor invalid_first{};
    invalid_first.block_size = static_cast<std::uint32_t>(invalid_block.size());
    invalid_first.hash = crypto::sha1(invalid_block);
    format.image_size = 32768;
    format.first_normal_block = invalid_first;
    output = {0x5A};
    if (XexImageLoader::decompress_normal_lzx(invalid_block, format, output) !=
            NormalDecompressError::LzxFailure ||
        output != std::vector<std::uint8_t>{0x5A}) {
        std::cerr << "XEX Normal malformed-LZX rejection failed\n";
        return EXIT_FAILURE;
    }

    invalid_block[26] ^= 1;
    if (XexImageLoader::decompress_normal_lzx(invalid_block, format, output) !=
        NormalDecompressError::InvalidBlockStream) {
        std::cerr << "XEX Normal block SHA-1 rejection failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "XEX Normal positive LZX integration and negative tests passed\n";
    return EXIT_SUCCESS;
}
