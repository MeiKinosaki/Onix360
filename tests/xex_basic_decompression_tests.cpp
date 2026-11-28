#include "onix360/xex/xex_header.hpp"

#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <vector>

static void put_be32(std::vector<std::uint8_t>& b, std::size_t o, std::uint32_t v) {
    b[o] = static_cast<std::uint8_t>(v >> 24);
    b[o + 1] = static_cast<std::uint8_t>(v >> 16);
    b[o + 2] = static_cast<std::uint8_t>(v >> 8);
    b[o + 3] = static_cast<std::uint8_t>(v);
}

int main() {
    std::vector<std::uint8_t> file(0x100, 0);
    file[0] = 'X'; file[1] = 'E'; file[2] = 'X'; file[3] = '2';
    put_be32(file, 8, 0x80); // compressed payload offset
    put_be32(file, 16, 0x40); // security info offset
    put_be32(file, 20, 2); // two optional headers
    put_be32(file, 24, 0x00010100); put_be32(file, 28, 0x82001000);
    put_be32(file, 32, 0x000003FF); put_be32(file, 36, 0x50);
    put_be32(file, 0x44, 6); // image size
    put_be32(file, 0x50, 24); // file format info size
    file[0x54] = 0; file[0x55] = 0; // no encryption
    file[0x56] = 0; file[0x57] = 1; // Basic compression
    put_be32(file, 0x58, 3); // data bytes
    put_be32(file, 0x5C, 3); // zero bytes
    file[0x80] = 'P'; file[0x81] = 'E'; file[0x82] = 'X';

    onix360::xex::Header header{};
    if (onix360::xex::parse_header(file, header, file.size()) != onix360::xex::ParseResult::Ok) {
        std::cerr << "XEX header parse failed\n"; return EXIT_FAILURE;
    }
    onix360::xex::FileFormatInfo format{};
    if (onix360::xex::parse_file_format_info(file, header, format) !=
        onix360::xex::FormatParseResult::Ok) {
        std::cerr << "XEX file-format metadata parse failed\n"; return EXIT_FAILURE;
    }
    std::vector<std::uint8_t> decoded;
    if (onix360::xex::decompress_basic(file, header, format, decoded) !=
        onix360::xex::BasicDecompressResult::Ok ||
        decoded.size() != 6 || decoded[0] != 'P' || decoded[1] != 'E' ||
        decoded[2] != 'X' || decoded[3] != 0 || decoded[5] != 0) {
        std::cerr << "XEX Basic decompression output mismatch\n"; return EXIT_FAILURE;
    }
    format.encryption = onix360::xex::XexEncryption::Normal;
    if (onix360::xex::decompress_basic(file, header, format, decoded) !=
        onix360::xex::BasicDecompressResult::EncryptedInput) {
        std::cerr << "Encrypted XEX must not be passed to the Basic decoder\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
