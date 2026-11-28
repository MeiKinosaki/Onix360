#include "onix360/xex/xex_image_loader.hpp"
#include "onix360/crypto/sha1.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#ifndef Onix360_TEST_DATA_DIR
#error Onix360_TEST_DATA_DIR must point to generated LZX reference vectors
#endif

static void be32(std::vector<std::uint8_t>& b, std::size_t p, std::uint32_t v) {
    b[p] = static_cast<std::uint8_t>(v >> 24);
    b[p + 1] = static_cast<std::uint8_t>(v >> 16);
    b[p + 2] = static_cast<std::uint8_t>(v >> 8);
    b[p + 3] = static_cast<std::uint8_t>(v);
}

int main() {
    using namespace onix360;
    using namespace onix360::xex;

    const std::string data_dir = Onix360_TEST_DATA_DIR;
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

    // Exercise the complete Normal transform path: wrap a session key with
    // the devkit master key, AES-CBC encrypt the framed block, then invoke the
    // production unwrap/decrypt/SHA-1/deblock/LZX pipeline.
    const crypto::Aes128Key session_key{
        0x2b,0x7e,0x15,0x16,0x28,0xae,0xd2,0xa6,
        0xab,0xf7,0x15,0x88,0x09,0xcf,0x4f,0x3c};
    auto encrypted_session_key = session_key;
    const crypto::Aes128Key retail_master = crypto::xex_master_key(crypto::XexKeySet::Retail);
    AES_ctx wrap_context{};
    AES_init_ctx(&wrap_context, retail_master.data());
    AES_ECB_encrypt(&wrap_context, encrypted_session_key.data());

    auto encrypted_normal_block = block;
    encrypted_normal_block.resize(
        (encrypted_normal_block.size() + 15u) & ~std::size_t(15u), 0);
    const crypto::Aes128Key zero_iv{};
    AES_ctx payload_context{};
    AES_init_ctx_iv(&payload_context, session_key.data(), zero_iv.data());
    AES_CBC_encrypt_buffer(&payload_context, encrypted_normal_block.data(),
                           static_cast<std::uint32_t>(encrypted_normal_block.size()));

    SecurityInfo security{};
    security.image_size = format.image_size;
    security.image_flags = kXexImagePageSize4Kb;
    security.encrypted_image_key = encrypted_session_key;
    format.encryption = XexEncryption::Normal;

    // Build a complete reference page-descriptor chain for the independently
    // decoded image. The final link is a fixed terminal digest; preceding links
    // authenticate the following page and its exact serialized descriptor.
    constexpr std::size_t page_size = 0x1000;
    const std::size_t page_count = (expected.size() + page_size - 1) / page_size;
    security.pages.resize(page_count);
    crypto::Sha1Digest next_digest{};
    next_digest.fill(0xA7);
    for (std::size_t i = page_count; i-- > 0;) {
        auto& descriptor = security.pages[i];
        descriptor.page_count = 1;
        descriptor.type = 1;
        descriptor.raw = 0x11;
        descriptor.digest = next_digest;
        const auto begin = i * page_size;
        const auto count = std::min(page_size, expected.size() - begin);
        std::vector<std::uint8_t> authenticated(page_size, 0);
        std::copy_n(reinterpret_cast<const std::uint8_t*>(expected.data()) + begin,
                    count, authenticated.begin());
        authenticated.insert(authenticated.end(), {
            static_cast<std::uint8_t>(descriptor.raw >> 24),
            static_cast<std::uint8_t>(descriptor.raw >> 16),
            static_cast<std::uint8_t>(descriptor.raw >> 8),
            static_cast<std::uint8_t>(descriptor.raw)});
        authenticated.insert(authenticated.end(), descriptor.digest.begin(),
                             descriptor.digest.end());
        next_digest = crypto::sha1(authenticated);
    }
    security.section_digest = next_digest;
    output = {0x5A};
    if (XexImageLoader::transform_normal_lzx_verified(
            security, crypto::XexKeySet::Retail, format,
            encrypted_normal_block, output) != NormalTransformError::Ok ||
        output != std::vector<std::uint8_t>(expected.begin(), expected.end())) {
        std::cerr << "XEX Normal AES/LZX/page-hash verified pipeline failed\\n";
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
