#include "wx360/storage/svod_gdf.hpp"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
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
    auto bytes = std::vector<std::uint8_t>(0x12000, 0);
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

    // Root block 4 maps to 0xF000 and contains a "game" directory.
    const std::size_t root_entry = 0xF000;
    put_le16(bytes, root_entry, 0); // left node
    put_le16(bytes, root_entry + 2, 0); // right node
    put_le32(bytes, root_entry + 4, 8); // child directory block
    put_le32(bytes, root_entry + 8, 1); // non-empty directory
    bytes[root_entry + 12] = 0x10; // directory attribute
    const std::string directory_name = "game";
    bytes[root_entry + 13] = static_cast<std::uint8_t>(directory_name.size());
    std::copy(directory_name.begin(), directory_name.end(), bytes.begin() + root_entry + 14);

    // Child block 8 maps to 0x11000 and contains default.xex.
    const std::size_t child_entry = 0x11000;
    put_le16(bytes, child_entry, 0);
    put_le16(bytes, child_entry + 2, 0);
    put_le32(bytes, child_entry + 4, 6); // file data block
    put_le32(bytes, child_entry + 8, 5); // file length
    bytes[child_entry + 12] = 0; // normal file
    const std::string name = "default.xex";
    bytes[child_entry + 13] = static_cast<std::uint8_t>(name.size());
    std::copy(name.begin(), name.end(), bytes.begin() + child_entry + 14);

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
    if (!parsed || parsed.entries.size() != 2 ||
        parsed.entries[1].relative_path.generic_string() != "game/default.xex" ||
        parsed.entries[1].size != payload.size()) {
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
    std::ifstream extracted(destination / "game" / "default.xex", std::ios::binary);
    const std::string actual((std::istreambuf_iterator<char>(extracted)), {});
    std::filesystem::remove_all(root, ec);
    if (actual != payload) {
        std::cerr << "SVOD extracted payload mismatch\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
