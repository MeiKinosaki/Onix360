#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <utility>

namespace wx360::storage {

enum class StfsPackageType { ConsoleSigned, XboxLiveSigned, MicrosoftSigned };
enum class StfsVolumeType { Stfs, Svod };

struct StfsPackageHeader {
    StfsPackageType package_type{};
    StfsVolumeType volume_type{};
    std::uint32_t header_size{};
    std::uint32_t content_type{};
    std::uint32_t metadata_version{};
    std::uint64_t content_size{};
    std::uint32_t title_id{};
    std::uint16_t file_table_block_count{};
    std::uint32_t file_table_start_block{};
    std::uint32_t allocated_block_count{};
    std::uint32_t unallocated_block_count{};
    std::uint32_t data_file_count{};
    std::uint64_t data_file_size{};
    std::uint8_t volume_flags{};
    bool read_only_format{};
    std::string title_name;
    std::string publisher_name;
};

enum class StfsParseError {
    None,
    CannotOpen,
    FileTooSmall,
    InvalidMagic,
    InvalidHeaderSize,
    InvalidVolumeDescriptor,
    UnsupportedVolumeType,
    InvalidStfsBlockCounts,
    TruncatedHeader
};

struct StfsParseResult {
    StfsParseError error{StfsParseError::None};
    StfsPackageHeader header{};
    [[nodiscard]] explicit operator bool() const noexcept { return error == StfsParseError::None; }
};

class StfsPackageParser {
public:
    static constexpr std::size_t kMetadataHeaderSize = 0x971A;
    static constexpr std::size_t kMinimumHeaderSize = 0x3AD;
    static constexpr std::size_t kVolumeDescriptorOffset = 0x379;

    [[nodiscard]] static StfsParseResult parse(const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) return {StfsParseError::CannotOpen, {}};

        file.seekg(0, std::ios::end);
        const auto end = file.tellg();
        if (end < 0 || static_cast<std::uint64_t>(end) < kMetadataHeaderSize)
            return {StfsParseError::FileTooSmall, {}};
        const auto file_size = static_cast<std::uint64_t>(end);

        std::array<std::uint8_t, kMetadataHeaderSize> bytes{};
        file.seekg(0, std::ios::beg);
        file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!file) return {StfsParseError::TruncatedHeader, {}};

        StfsPackageHeader header{};
        if (bytes[0] == 'C' && bytes[1] == 'O' && bytes[2] == 'N' && bytes[3] == ' ')
            header.package_type = StfsPackageType::ConsoleSigned;
        else if (bytes[0] == 'L' && bytes[1] == 'I' && bytes[2] == 'V' && bytes[3] == 'E')
            header.package_type = StfsPackageType::XboxLiveSigned;
        else if (bytes[0] == 'P' && bytes[1] == 'I' && bytes[2] == 'R' && bytes[3] == 'S')
            header.package_type = StfsPackageType::MicrosoftSigned;
        else
            return {StfsParseError::InvalidMagic, {}};

        header.header_size = be32(bytes.data() + 0x340);
        if (header.header_size < kMetadataHeaderSize || header.header_size > file_size)
            return {StfsParseError::InvalidHeaderSize, {}};

        header.content_type = be32(bytes.data() + 0x344);
        header.metadata_version = be32(bytes.data() + 0x348);
        header.content_size = be64(bytes.data() + 0x34C);
        header.title_id = be32(bytes.data() + 0x360);

        const auto* volume = bytes.data() + kVolumeDescriptorOffset;
        if (volume[0] != 0x24) return {StfsParseError::InvalidVolumeDescriptor, {}};
        const auto volume_type = be32(bytes.data() + 0x3A9);
        if (volume_type > 1) return {StfsParseError::UnsupportedVolumeType, {}};
        header.volume_type = volume_type == 0 ? StfsVolumeType::Stfs : StfsVolumeType::Svod;
        header.data_file_count = be32(bytes.data() + 0x39D);
        header.data_file_size = be64(bytes.data() + 0x3A1);
        header.volume_flags = volume[2];
        header.read_only_format = (header.volume_flags & 0x01u) != 0;

        // The 0x24-byte STFS descriptor has LE16/LE24 block fields.
        // SVOD has a different descriptor layout and is deliberately not
        // interpreted by this STFS-only parser.
        if (header.volume_type == StfsVolumeType::Svod)
            return {StfsParseError::UnsupportedVolumeType, {}};

        if (volume[0] != 0x24)
            return {StfsParseError::InvalidVolumeDescriptor, {}};
        header.file_table_block_count = le16(volume + 3);
        header.file_table_start_block = le24(volume + 5);
        header.allocated_block_count = be32(volume + 0x1C);
        header.unallocated_block_count = be32(volume + 0x20);
        if (header.file_table_block_count > header.allocated_block_count ||
            header.allocated_block_count > 0x00FFFFFFu ||
            header.unallocated_block_count > 0x00FFFFFFu ||
            (header.file_table_block_count != 0 &&
             header.file_table_start_block >= header.allocated_block_count))
            return {StfsParseError::InvalidStfsBlockCounts, {}};

        header.title_name = read_utf16be_ascii(bytes.data() + 0x1691, 0x80);
        header.publisher_name = read_utf16be_ascii(bytes.data() + 0x1611, 0x80);
        return {StfsParseError::None, std::move(header)};
    }

private:
    static std::uint16_t le16(const std::uint8_t* p) {
        return static_cast<std::uint16_t>(p[0] | (static_cast<std::uint16_t>(p[1]) << 8));
    }
    static std::uint32_t le24(const std::uint8_t* p) {
        return static_cast<std::uint32_t>(p[0]) |
               (static_cast<std::uint32_t>(p[1]) << 8) |
               (static_cast<std::uint32_t>(p[2]) << 16);
    }
    static std::uint32_t be32(const std::uint8_t* p) {
        return (static_cast<std::uint32_t>(p[0]) << 24) |
               (static_cast<std::uint32_t>(p[1]) << 16) |
               (static_cast<std::uint32_t>(p[2]) << 8) |
               static_cast<std::uint32_t>(p[3]);
    }
    static std::uint64_t be64(const std::uint8_t* p) {
        return (static_cast<std::uint64_t>(be32(p)) << 32) | be32(p + 4);
    }
    static std::string read_utf16be_ascii(const std::uint8_t* p, std::size_t bytes) {
        std::string result;
        for (std::size_t i = 0; i + 1 < bytes; i += 2) {
            const auto code = static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(p[i]) << 8) | p[i + 1]);
            if (code == 0) break;
            result.push_back(code >= 0x20 && code <= 0x7E ? static_cast<char>(code) : '?');
        }
        return result;
    }
};

} // namespace wx360::storage
