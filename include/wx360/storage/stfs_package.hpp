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

    [[nodiscard]] static StfsFileTableResult read_file_table(
        const std::filesystem::path& path, const StfsPackageHeader& header) {
        if (header.volume_type != StfsVolumeType::Stfs || !header.read_only_format)
            return {StfsParseError::UnsupportedWritableStfsLayout, {}};
        if (header.file_table_block_count == 0 ||
            header.file_table_block_count > header.allocated_block_count ||
            header.file_table_start_block >= header.allocated_block_count)
            return {StfsParseError::InvalidFileTableChain, {}};

        std::ifstream file(path, std::ios::binary);
        if (!file) return {StfsParseError::CannotOpen, {}};
        file.seekg(0, std::ios::end);
        const auto end = file.tellg();
        if (end < 0) return {StfsParseError::TruncatedHeader, {}};
        const auto file_size = static_cast<std::uint64_t>(end);
        const std::uint64_t data_base =
            (static_cast<std::uint64_t>(header.header_size) + 0xFFFu) & ~std::uint64_t(0xFFFu);

        const auto block_offset = [&](std::uint32_t index) -> std::uint64_t {
            std::uint64_t block = index;
            std::uint64_t base = 170;
            for (unsigned level = 0; level < 3; ++level) {
                block += ((static_cast<std::uint64_t>(index) + base) / base);
                if (index < base) break;
                base *= 170;
            }
            return data_base + (block << 12);
        };
        const auto hash_block_number = [](std::uint32_t index) -> std::uint64_t {
            constexpr std::uint64_t kLevel0 = 170;
            constexpr std::uint64_t kLevel1 = 28900;
            constexpr std::uint64_t kBlockStep0 = 171;
            constexpr std::uint64_t kBlockStep1 = 28900 + 171;
            if (index < kLevel0) return 0;
            std::uint64_t block = (index / kLevel0) * kBlockStep0;
            block += (index / kLevel1 + 1);
            if (index < kLevel1) return block;
            return block + 1;
        };

        std::uint32_t current_table_block = header.file_table_start_block;
        std::vector<StfsFileEntry> entries;
        const std::uint64_t max_entries =
            static_cast<std::uint64_t>(header.file_table_block_count) * 64u;

        for (std::uint32_t table = 0; table < header.file_table_block_count; ++table) {
            if (current_table_block >= header.allocated_block_count)
                return {StfsParseError::InvalidFileTableChain, {}};
            const auto offset = block_offset(current_table_block);
            if (offset > file_size || file_size - offset < 0x1000u)
                return {StfsParseError::TruncatedHeader, {}};

            std::array<std::uint8_t, 0x1000> directory_block{};
            file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
            file.read(reinterpret_cast<char*>(directory_block.data()),
                      static_cast<std::streamsize>(directory_block.size()));
            if (!file) return {StfsParseError::TruncatedHeader, {}};

            for (std::size_t slot = 0; slot < 64; ++slot) {
                const auto* raw = directory_block.data() + slot * 0x40;
                const auto name_length = static_cast<std::size_t>(raw[0x28] & 0x3Fu);
                if (name_length == 0 || raw[0x28] == 0) break;
                if (name_length > 40 || entries.size() >= max_entries)
                    return {StfsParseError::InvalidDirectoryEntry, {}};

                StfsFileEntry entry{};
                entry.name.assign(reinterpret_cast<const char*>(raw), name_length);
                entry.directory = (raw[0x28] & 0x80u) != 0;
                entry.contiguous = (raw[0x28] & 0x40u) != 0;
                entry.valid_blocks = le24(raw + 0x29);
                entry.allocated_blocks = le24(raw + 0x2C);
                entry.starting_block = le24(raw + 0x2F);
                entry.parent_directory_index = be16(raw + 0x32);
                entry.file_size = be32(raw + 0x34);
                if (!entry.directory && entry.file_size != 0 &&
                    (entry.allocated_blocks == 0 ||
                     entry.starting_block >= header.allocated_block_count ||
                     entry.allocated_blocks > header.allocated_block_count))
                    return {StfsParseError::InvalidDirectoryEntry, {}};
                entries.push_back(std::move(entry));
            }

            if (table + 1u == header.file_table_block_count) break;
            const auto hash_offset = data_base +
                (hash_block_number(current_table_block) << 12) +
                (static_cast<std::uint64_t>(current_table_block % 170u) * 0x18u) + 0x14u;
            if (hash_offset > file_size || file_size - hash_offset < 4u)
                return {StfsParseError::TruncatedHeader, {}};
            std::array<std::uint8_t, 4> info{};
            file.seekg(static_cast<std::streamoff>(hash_offset), std::ios::beg);
            file.read(reinterpret_cast<char*>(info.data()), 4);
            if (!file) return {StfsParseError::TruncatedHeader, {}};
            const auto next = be32(info.data()) & 0x00FFFFFFu;
            if (next == 0x00FFFFFFu || next >= header.allocated_block_count ||
                next == current_table_block)
                return {StfsParseError::InvalidFileTableChain, {}};
            current_table_block = next;
        }

        for (const auto& entry : entries) {
            if (entry.parent_directory_index != 0xFFFFu &&
                entry.parent_directory_index >= entries.size())
                return {StfsParseError::InvalidDirectoryEntry, {}};
        }
        return {StfsParseError::None, std::move(entries)};
    }

private:
    static std::uint16_t le16(const std::uint8_t* p) {
        return static_cast<std::uint16_t>(p[0] | (static_cast<std::uint16_t>(p[1]) << 8));
    }
    static std::uint16_t be16(const std::uint8_t* p) {
        return static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[0]) << 8) | p[1]);
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
