#include "wx360/xex/xex_image_loader.hpp"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

static void put_be32(std::vector<std::uint8_t>& b, std::size_t o, std::uint32_t v) {
    b[o] = static_cast<std::uint8_t>(v >> 24);
    b[o + 1] = static_cast<std::uint8_t>(v >> 16);
    b[o + 2] = static_cast<std::uint8_t>(v >> 8);
    b[o + 3] = static_cast<std::uint8_t>(v);
}

int main() {
    std::vector<std::uint8_t> bytes(0x20000, 0);
    bytes[0] = 'X'; bytes[1] = 'E'; bytes[2] = 'X'; bytes[3] = '2';
    put_be32(bytes, 8, 0x10000); // PE data offset beyond bounded header window
    put_be32(bytes, 16, 0x11000);
    put_be32(bytes, 20, 1);
    put_be32(bytes, 24, 0x00010100);
    put_be32(bytes, 28, 0x82000000);
    const auto path = std::filesystem::temp_directory_path() / "wx360_xex_loader_test.xex";
    {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
    }
    const auto result = wx360::xex::XexImageLoader::inspect(path);
    std::error_code ec;
    std::filesystem::remove(path, ec);
    if (!result || result.plan.entry_point != 0x82000000 ||
        result.plan.file_size != bytes.size()) {
        std::cerr << "XEX launch plan inspection failed\n";
        return EXIT_FAILURE;
    }
    // An XEX that declares imports must not be treated as validated unless
    // both the kernel export registry and guest MMU are supplied. The early
    // rejection must leave caller RAM and mappings untouched.
    wx360::memory::PhysicalMemory guarded_ram(0x4000);
    const std::array<std::uint8_t, 4> guard{{0x11, 0x22, 0x33, 0x44}};
    if (guarded_ram.write(0x1000, guard) != wx360::memory::MemoryResult::Ok)
        return EXIT_FAILURE;
    wx360::memory::AddressSpace guarded_as;
    wx360::xex::FullXexImage import_image{};
    import_image.header.has_import_libraries = true;
    const auto rejected_import_image = wx360::xex::XexImageLoader::load_validated(
        import_image, guarded_ram, 0x1000, 0x82000000);
    std::array<std::uint8_t, 4> after_guard{};
    if (rejected_import_image ||
        guarded_ram.read(0x1000, after_guard) != wx360::memory::MemoryResult::Ok ||
        after_guard != guard || guarded_as.page_count() != 0) {
        std::cerr << "XEX loader accepted unresolved imports or mutated guest state\\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
