#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
#include <algorithm>
#include <utility>

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
    std::uint32_t file_format_info_offset{};
    bool has_file_format_info{};
    std::uint32_t import_libraries_offset{};
    bool has_import_libraries{};
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
        if (key == 0x000103FFU) {
            parsed.import_libraries_offset = value;
            parsed.has_import_libraries = true;
        }
        if (key == 0x000003FFU) {
            parsed.file_format_info_offset = value;
            parsed.has_file_format_info = true;
        }
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


enum class XexEncryption : std::uint16_t { None = 0, Normal = 1 };
enum class XexCompression : std::uint16_t { None = 0, Basic = 1, Normal = 2 };

struct BasicBlock {
    std::uint32_t data_size{};
    std::uint32_t zero_size{};
};

struct FileFormatInfo {
    std::uint32_t info_size{};
    XexEncryption encryption{XexEncryption::None};
    XexCompression compression{XexCompression::None};
    std::uint32_t image_size{};
    std::vector<BasicBlock> basic_blocks;
};

enum class FormatParseResult : std::uint8_t {
    Ok, MissingInfo, Truncated, UnsupportedType, InvalidSizes
};

[[nodiscard]] inline FormatParseResult parse_file_format_info(
    std::span<const std::uint8_t> file, const Header& header,
    FileFormatInfo& output) {
    if (!header.has_file_format_info) return FormatParseResult::MissingInfo;
    const auto offset = static_cast<std::size_t>(header.file_format_info_offset);
    if (offset > file.size() || file.size() - offset < 8)
        return FormatParseResult::Truncated;
    FileFormatInfo parsed;
    parsed.info_size = read_be32(file, offset);
    if (parsed.info_size < 8 || parsed.info_size > file.size() - offset)
        return FormatParseResult::InvalidSizes;
    parsed.encryption = static_cast<XexEncryption>(
        (static_cast<std::uint16_t>(file[offset + 4]) << 8) | file[offset + 5]);
    parsed.compression = static_cast<XexCompression>(
        (static_cast<std::uint16_t>(file[offset + 6]) << 8) | file[offset + 7]);
    if ((parsed.encryption != XexEncryption::None &&
         parsed.encryption != XexEncryption::Normal) ||
        (parsed.compression != XexCompression::None &&
         parsed.compression != XexCompression::Basic &&
         parsed.compression != XexCompression::Normal))
        return FormatParseResult::UnsupportedType;
    if (header.security_info_offset > file.size() ||
        file.size() - header.security_info_offset < 8)
        return FormatParseResult::Truncated;
    parsed.image_size = read_be32(file, header.security_info_offset + 4);
    if (parsed.image_size == 0 || parsed.image_size > 512u * 1024u * 1024u)
        return FormatParseResult::InvalidSizes;
    if (parsed.compression == XexCompression::Basic) {
        if ((parsed.info_size - 8u) % 8u != 0)
            return FormatParseResult::InvalidSizes;
        const auto count = (parsed.info_size - 8u) / 8u;
        if (count == 0 || count > 65536u)
            return FormatParseResult::InvalidSizes;
        std::uint64_t total = 0;
        parsed.basic_blocks.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            const auto at = offset + 8u + static_cast<std::size_t>(i) * 8u;
            BasicBlock block{read_be32(file, at), read_be32(file, at + 4)};
            total += static_cast<std::uint64_t>(block.data_size) + block.zero_size;
            if (total > parsed.image_size) return FormatParseResult::InvalidSizes;
            parsed.basic_blocks.push_back(block);
        }
        if (total != parsed.image_size) return FormatParseResult::InvalidSizes;
    } else if (parsed.info_size != 8) {
        return FormatParseResult::InvalidSizes;
    }
    output = std::move(parsed);
    return FormatParseResult::Ok;
}

enum class BasicDecompressResult : std::uint8_t {
    Ok, EncryptedInput, UnsupportedCompression, TruncatedInput, SizeMismatch
};

// Supports only unencrypted XEX2 Basic compression. Normal AES/LZX paths
// remain explicitly unsupported until their crypto/decoder implementations
// are integrated and validated.
[[nodiscard]] inline BasicDecompressResult decompress_basic(
    std::span<const std::uint8_t> file, const Header& header,
    const FileFormatInfo& format, std::vector<std::uint8_t>& output) {
    if (format.encryption != XexEncryption::None)
        return BasicDecompressResult::EncryptedInput;
    if (format.compression != XexCompression::Basic)
        return BasicDecompressResult::UnsupportedCompression;
    if (header.pe_data_offset > file.size())
        return BasicDecompressResult::TruncatedInput;
    std::vector<std::uint8_t> decoded(format.image_size, 0);
    std::size_t source = header.pe_data_offset;
    std::size_t destination = 0;
    for (const auto& block : format.basic_blocks) {
        if (block.data_size > file.size() - source ||
            block.data_size > decoded.size() - destination ||
            block.zero_size > decoded.size() - destination - block.data_size)
            return BasicDecompressResult::TruncatedInput;
        std::copy_n(file.begin() + static_cast<std::ptrdiff_t>(source),
                    block.data_size,
                    decoded.begin() + static_cast<std::ptrdiff_t>(destination));
        source += block.data_size;
        destination += block.data_size + block.zero_size;
    }
    if (destination != decoded.size()) return BasicDecompressResult::SizeMismatch;
    output = std::move(decoded);
    return BasicDecompressResult::Ok;
}

} // namespace wx360::xex
