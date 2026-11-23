#include "wx360/xex/xex_image_loader.hpp"
#include "wx360/crypto/sha1.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

static void be32(std::vector<std::uint8_t>& b, std::size_t p, std::uint32_t v) {
    b[p] = static_cast<std::uint8_t>(v >> 24);
    b[p + 1] = static_cast<std::uint8_t>(v >> 16);
    b[p + 2] = static_cast<std::uint8_t>(v >> 8);
    b[p + 3] = static_cast<std::uint8_t>(v);
}

int main() {
    using namespace wx360;
    using namespace wx360::xex;

    // One correctly framed/authenticated block containing a deliberately
    // invalid one-byte LZX payload followed by the zero chunk terminator.
    std::vector<std::uint8_t> block(24 + 2 + 1 + 2, 0);
    block[24] = 0; block[25] = 1;
    block[26] = 0;
    block[27] = 0; block[28] = 0;
    NormalBlockDescriptor first{};
    first.block_size = static_cast<std::uint32_t>(block.size());
    first.hash = crypto::sha1(block);

    FileFormatInfo format{};
    format.compression = XexCompression::Normal;
    format.normal_window_size = 0x20000;
    format.image_size = 32768;
    format.first_normal_block = first;

    std::vector<std::uint8_t> output{0x5A};
    if (XexImageLoader::decompress_normal_lzx(block, format, output) !=
            NormalDecompressError::LzxFailure ||
        output != std::vector<std::uint8_t>{0x5A}) {
        std::cerr << "XEX Normal malformed-LZX rejection failed\n";
        return EXIT_FAILURE;
    }

    block[26] ^= 1;
    if (XexImageLoader::decompress_normal_lzx(block, format, output) !=
        NormalDecompressError::InvalidBlockStream) {
        std::cerr << "XEX Normal block SHA-1 rejection failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "XEX Normal framing/hash negative tests passed\n";
    return EXIT_SUCCESS;
}
