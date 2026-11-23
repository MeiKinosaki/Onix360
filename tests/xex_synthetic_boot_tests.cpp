#include "wx360/machine/xenon_core.hpp"
#include "wx360/xex/xex_image_loader.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace {
void put16(std::vector<std::uint8_t>& b, std::size_t o, std::uint16_t v) {
    b[o] = static_cast<std::uint8_t>(v); b[o + 1] = static_cast<std::uint8_t>(v >> 8);
}
void put32(std::vector<std::uint8_t>& b, std::size_t o, std::uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) b[o + i] = static_cast<std::uint8_t>(v >> (8 * i));
}
void put_be32(std::vector<std::uint8_t>& b, std::size_t o, std::uint32_t v) {
    b[o] = static_cast<std::uint8_t>(v >> 24); b[o + 1] = static_cast<std::uint8_t>(v >> 16);
    b[o + 2] = static_cast<std::uint8_t>(v >> 8); b[o + 3] = static_cast<std::uint8_t>(v);
}
std::vector<std::uint8_t> make_synthetic_xbox_pe() {
    constexpr std::size_t pe = 0x80, coff = pe + 4, optional = coff + 20;
    constexpr std::size_t section = optional + 224, raw = 0x200;
    std::vector<std::uint8_t> image(0x400, 0);
    put16(image, 0, 0x5A4D); put32(image, 0x3C, static_cast<std::uint32_t>(pe));
    put32(image, pe, 0x00004550); put16(image, coff, 0x01F2);
    put16(image, coff + 2, 1); put16(image, coff + 16, 224);
    put16(image, optional, 0x010B); put32(image, optional + 16, 0x1000);
    put32(image, optional + 28, 0x82000000); put32(image, optional + 32, 0x1000);
    put32(image, optional + 36, 0x200); put32(image, optional + 56, 0x2000);
    put32(image, optional + 60, 0x200); put16(image, optional + 68, 14);
    put32(image, optional + 92, 16);
    image[section] = '.'; image[section + 1] = 't'; image[section + 2] = 'e';
    image[section + 3] = 'x'; image[section + 4] = 't';
    put32(image, section + 8, 0x100); put32(image, section + 12, 0x1000);
    put32(image, section + 16, 0x200); put32(image, section + 20, static_cast<std::uint32_t>(raw));
    put32(image, section + 36, 0x60000020);
    image[raw] = 0x38; image[raw + 1] = 0x60; image[raw + 2] = 0x00; image[raw + 3] = 0x2A;
    return image;
}
std::vector<std::uint8_t> make_synthetic_xex2(const std::vector<std::uint8_t>& pe) {
    constexpr std::size_t format_info = 0x80, security_info = 0x100, pe_offset = 0x300;
    std::vector<std::uint8_t> xex(pe_offset + pe.size(), 0);
    xex[0] = 'X'; xex[1] = 'E'; xex[2] = 'X'; xex[3] = '2';
    put_be32(xex, 8, pe_offset); put_be32(xex, 16, security_info); put_be32(xex, 20, 2);
    put_be32(xex, 24, 0x00010100); put_be32(xex, 28, 0x82001000);
    put_be32(xex, 32, 0x000003FF); put_be32(xex, 36, format_info);
    put_be32(xex, format_info, 8);
    put_be32(xex, security_info, 0x184);
    put_be32(xex, security_info + 4, static_cast<std::uint32_t>(pe.size()));
    std::copy(pe.begin(), pe.end(), xex.begin() + pe_offset);
    return xex;
}
}

int main() {
    using namespace wx360;
    const auto pe = make_synthetic_xbox_pe();
    const auto xex_bytes = make_synthetic_xex2(pe);
    const auto path = std::filesystem::temp_directory_path() / "wx360_synthetic_boot.xex";
    {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(xex_bytes.data()),
                  static_cast<std::streamsize>(xex_bytes.size()));
        if (!out) return EXIT_FAILURE;
    }
    const auto inspected = xex::XexImageLoader::inspect(path);
    std::error_code ec; std::filesystem::remove(path, ec);
    if (!inspected || inspected.plan.entry_point != 0x82001000) {
        std::cerr << "Synthetic XEX2 header inspection failed\n"; return EXIT_FAILURE;
    }
    xex::Header header{};
    if (xex::parse_header(xex_bytes, header, xex_bytes.size()) != xex::ParseResult::Ok) {
        std::cerr << "Synthetic XEX2 parse failed\n"; return EXIT_FAILURE;
    }
    xex::FileFormatInfo format{};
    if (xex::parse_file_format_info(xex_bytes, header, format) != xex::FormatParseResult::Ok ||
        format.image_size != pe.size() || format.compression != xex::XexCompression::None ||
        format.encryption != xex::XexEncryption::None) {
        std::cerr << "Synthetic XEX2 file-format metadata failed\n"; return EXIT_FAILURE;
    }
    memory::PhysicalMemory ram(0x10000);
    auto plan = inspected.plan;
    constexpr std::uint64_t physical_base = 0x2000;
    constexpr std::uint32_t guest_base = 0x82000000;
    const auto mapped = xex::XexImageLoader::map_transformed_pe(
        plan, std::span<const std::uint8_t>(xex_bytes).subspan(0x300, pe.size()),
        ram, physical_base, guest_base, true, true);
    if (mapped != xex::LoadError::None || !plan.pe_mapped) {
        std::cerr << "Synthetic XEX2 PE mapping failed\n"; return EXIT_FAILURE;
    }
    // The fixture has no imports, but still exercise the production resolver
    // instead of manually asserting that import resolution succeeded.
    kernel::XexImportResolver resolver;
    const std::vector<kernel::XexImport> imports;
    if (xex::XexImageLoader::resolve_kernel_imports(plan, imports, resolver, ram) !=
            kernel::ImportResolveError::None ||
        !plan.kernel_imports_resolved) {
        std::cerr << "Synthetic XEX2 import-resolution stage failed\\n";
        return EXIT_FAILURE;
    }
    machine::XenonCore core;
    if (core.start_xex(plan, ram) != machine::LaunchResult::Started) {
        std::cerr << "XenonCore rejected the mapped synthetic XEX2\n"; return EXIT_FAILURE;
    }
    if (core.step(ram) != machine::RunResult::Executed ||
        core.state.gpr[3] != 42 || core.state.pc != plan.mapped_entry_point + 4) {
        std::cerr << "First Xenon instruction did not execute from synthetic XEX2\n";
        return EXIT_FAILURE;
    }
    std::cout << "WX360 synthetic XEX2-to-first-Xenon-instruction test passed\n";
    return EXIT_SUCCESS;
}
