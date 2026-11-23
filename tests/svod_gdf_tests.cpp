#include "wx360/storage/svod_gdf.hpp"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

static void put_le16(std::vector<std::uint8_t>& b, std::size_t o, std::uint16_t v) {
    b[o] = static_cast<std::uint8_t>(v); b[o + 1] = static_cast<std::uint8_t>(v >> 8);
}
static void put_le32(std::vector<std::uint8_t>& b, std::size_t o, std::uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) b[o + i] = static_cast<std::uint8_t>(v >> (8 * i));
}
static void put_be32(std::vector<std::uint8_t>& b, std::size_t o, std::uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) b[o + i] = static_cast<std::uint8_t>(v >> (24 - 8 * i));
}

int main() {
    auto bytes = std::vector<std::uint8_t>(0x11000, 0);
    bytes[0] = 'P'; bytes[1] = 'I'; bytes[2] = 'R'; bytes[3] = 'S';
    put_be32(bytes, 0x340, 0x971A);
    bytes[0x379] = 0x24;
    put_be32(bytes, 0x39D, 1); // one self-contained SVOD data file
    bytes[0x3A9] = 1; // SVOD
    bytes[0x392] = 1; // one SVOD data block
    put_be32(bytes, 0x3A9, 1);
    bytes[0x379 + 0x18] = 0; // standard single-file layout
    bytes[0x379 + 0x19] = 1;
    bytes[0x379 + 0x1C] = 0;

    constexpr std::size_t magic = 0xD000;
    const char media[] = "MICROSOFT*XBOX*MEDIA";
    std::copy(media, media + 20, bytes.begin() + magic);
    put_le32(bytes, magic + 0x14, 4); // root directory block
    put_le32(bytes, magic + 0x18, 1);

    // SVOD block 4 maps to 0xF000? (hash tables + single-file header).
    const std::size_t entry = 0xF000;
    put_le16(bytes, entry, 0); // left node
    put_le16(bytes, entry + 2, 0); // right node
    put_le32(bytes, entry + 4, 6); // file data block
    put_le32(bytes, entry + 8, 5); // file length
    bytes[entry + 12] = 0; // normal file
    const std::string name = "boot.xex";
    bytes[entry + 13] = static_cast<std::uint8_t>(name.size());
    std::copy(name.begin(), name.end(), bytes.begin() + entry + 14);
    // SVOD block 6 maps to 0x10000 in a single-file image.
    const std::string payload = "XEX2!";
    std::copy(payload.begin(), payload.end(), bytes.begin() + 0x10000);

    const auto root = std::filesystem::temp_directory_path() / "wx360_svod_single_file_test";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    const auto package = root / "sample.svod";
    {
        std::ofstream out(package, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
    }

    const auto parsed = wx360::storage::SvodGdfReader::read(package);
    if (!parsed || parsed.entries.size() != 1 ||
        parsed.entries[0].relative_path.generic_string() != "boot.xex" ||
        parsed.entries[0].size != payload.size()) {
        std::cerr << "SVOD GDF directory parsing failed\n";
        std::filesystem::remove_all(root, ec);
        return EXIT_FAILURE;
    }

    wx360::storage::StfsParseError error{};
    const auto destination = root / "extracted";
    if (!wx360::storage::SvodGdfReader::extract(package, destination, error)) {
        std::cerr << "SVOD GDF extraction failed: " << static_cast<int>(error) << "\n";
        std::filesystem::remove_all(root, ec);
        return EXIT_FAILURE;
    }
    std::ifstream extracted(destination / "boot.xex", std::ios::binary);
    const std::string actual((std::istreambuf_iterator<char>(extracted)), {});
    std::filesystem::remove_all(root, ec);
    if (actual != payload) {
        std::cerr << "SVOD extracted payload mismatch\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
