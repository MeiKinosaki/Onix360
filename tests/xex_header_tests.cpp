#include "onix360/xex/xex_header.hpp"

#include <array>
#include <cstdlib>
#include <iostream>

namespace {
int failures = 0;
void check(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
void put_be32(std::array<std::uint8_t, 64>& bytes, std::size_t at, std::uint32_t value) {
    bytes[at] = static_cast<std::uint8_t>(value >> 24U);
    bytes[at + 1U] = static_cast<std::uint8_t>(value >> 16U);
    bytes[at + 2U] = static_cast<std::uint8_t>(value >> 8U);
    bytes[at + 3U] = static_cast<std::uint8_t>(value);
}
}

int main() {
    using namespace onix360::xex;
    std::array<std::uint8_t, 64> file{};
    file[0] = 'X'; file[1] = 'E'; file[2] = 'X'; file[3] = '2';
    put_be32(file, 8, 48);
    put_be32(file, 16, 40);
    put_be32(file, 20, 1);
    put_be32(file, 24, 0x00010100U);
    put_be32(file, 28, 0x00012000U);
    Header header{};
    check(parse_header(file, header) == ParseResult::Ok,
          "parse XEX2 fixed header and optional entry point");
    check(header.entry_point == 0x00012000U && header.has_entry_point,
          "extract entry point optional header");
    check(header.pe_data_offset == 48U, "extract PE data offset");

    auto bad_magic = file;
    bad_magic[0] = 'N';
    check(parse_header(bad_magic, header) == ParseResult::BadMagic,
          "reject non-XEX2 magic");
    auto truncated = file;
    put_be32(truncated, 20, 99);
    check(parse_header(truncated, header) == ParseResult::TruncatedHeader,
          "reject truncated optional-header directory");
    std::array<std::uint8_t, 8> short_file{};
    check(parse_header(short_file, header) == ParseResult::TooSmall,
          "reject undersized XEX file");
    if (failures == 0) std::cout << "Onix360 XEX header tests passed\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
