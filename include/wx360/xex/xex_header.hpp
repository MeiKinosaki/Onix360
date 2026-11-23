#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace wx360::xex {

enum class ParseResult : std::uint8_t {
    Ok,
    TooSmall,
    BadMagic,
    TruncatedHeader,
    MissingEntryPoint,
};

struct Header {
    std::uint32_t module_flags{};
    std::uint32_t pe_data_offset{};
    std::uint32_t security_info_offset{};
    std::uint32_t header_count{};
    std::uint32_t entry_point{};
    bool has_entry_point{};
};

[[nodiscard]] constexpr std::uint32_t read_be32(
    std::span<const std::uint8_t> bytes, std::size_t offset) noexcept {
    return (static_cast<std::uint32_t>(bytes[offset]) << 24U) |
           (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U) |
           static_cast<std::uint32_t>(bytes[offset + 3U]);
}

// Parses the fixed XEX2 header and optional-header directory only.
// This deliberately does not claim to decrypt/decompress or map PE sections.
[[nodiscard]] inline ParseResult parse_header(
    std::span<const std::uint8_t> file, Header& output,
    std::uint64_t full_file_size = 0) noexcept {
    constexpr std::size_t fixed_size = 24U;
    if (file.size() < fixed_size) return ParseResult::TooSmall;
    if (file[0] != 'X' || file[1] != 'E' || file[2] != 'X' || file[3] != '2')
        return ParseResult::BadMagic;

    Header parsed{};
    parsed.module_flags = read_be32(file, 4U);
    parsed.pe_data_offset = read_be32(file, 8U);
    parsed.security_info_offset = read_be32(file, 16U);
    parsed.header_count = read_be32(file, 20U);

    constexpr std::size_t entry_size = 8U;
    if (parsed.header_count > (file.size() - fixed_size) / entry_size)
        return ParseResult::TruncatedHeader;

    for (std::size_t i = 0; i < parsed.header_count; ++i) {
        const std::size_t offset = fixed_size + i * entry_size;
        const std::uint32_t key = read_be32(file, offset);
        const std::uint32_t value = read_be32(file, offset + 4U);
        if (key == 0x00010100U) {
            parsed.entry_point = value;
            parsed.has_entry_point = true;
        }
    }
    if (!parsed.has_entry_point) return ParseResult::MissingEntryPoint;
    const std::uint64_t image_size = full_file_size == 0 ? file.size() : full_file_size;
    if (parsed.pe_data_offset >= image_size ||
        parsed.security_info_offset >= image_size)
        return ParseResult::TruncatedHeader;
    output = parsed;
    return ParseResult::Ok;
}

} // namespace wx360::xex
