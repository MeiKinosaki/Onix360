#pragma once

#include "wx360/storage/stfs_package.hpp"
#include "wx360/storage/svod_container.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <string>
#include <vector>

namespace wx360::storage {

struct SvodGdfEntry {
    std::string name;
    bool directory{};
    std::uint32_t first_block{};
    std::uint32_t size{};
    std::filesystem::path relative_path;
};

struct SvodGdfResult {
    StfsParseError error{StfsParseError::None};
    SvodInspection inspection;
    std::vector<SvodGdfEntry> entries;
    [[nodiscard]] explicit operator bool() const noexcept {
        return error == StfsParseError::None;
    }
};

class SvodGdfReader {
public:
    static constexpr std::uint32_t kBlockSize = 0x800;
    static constexpr std::uint32_t kBlocksPerFragment = 0x14388;
    static constexpr std::uint32_t kFragmentPayloadSize = 0xA290000;
    static constexpr std::uint32_t kBlocksPerLevel0 = 0x198;
    static constexpr std::uint32_t kLevel0PerLevel1 = 0xA1C4;

    [[nodiscard]] static SvodGdfResult read(
        const std::filesystem::path& package_header,
        const std::filesystem::path& fragment_directory = {},
        std::size_t maximum_entries = 200000,
        std::size_t maximum_depth = 128) {
        SvodGdfResult result;
        result.inspection = SvodContainer::inspect(package_header, fragment_directory);
        if (!result.inspection) {
            result.error = result.inspection.error;
            return result;
        }
        auto fragments = result.inspection.fragments;
        if (fragments.empty()) {
            std::error_code ec;
            if (result.inspection.layout == SvodLayout::SingleFile)
                fragments.push_back(package_header);
            else {
                auto data_dir = fragment_directory;
                if (data_dir.empty()) {
                    data_dir = package_header;
                    data_dir += ".data";
                }
                if (!std::filesystem::is_directory(data_dir, ec) || ec) {
                    result.error = StfsParseError::UnsupportedSvodLayout;
                    return result;
                }
                for (std::filesystem::directory_iterator it(data_dir, ec), end;
                     it != end && !ec; it.increment(ec)) {
                    if (it->is_regular_file(ec) && !ec) fragments.push_back(it->path());
                    ec.clear();
                }
                std::sort(fragments.begin(), fragments.end());
            }
        }
        if (fragments.empty() ||
            (result.inspection.declared_fragment_count != 0 &&
             fragments.size() != result.inspection.declared_fragment_count)) {
            result.error = StfsParseError::UnsupportedSvodLayout;
            return result;
        }

        const auto magic_offset = result.inspection.layout == SvodLayout::EnhancedGdf
            ? 0x2000ull : (result.inspection.layout == SvodLayout::Xsf ? 0x12000ull : 0xD000ull);
        std::ifstream first(fragments.front(), std::ios::binary);
        if (!first) { result.error = StfsParseError::CannotOpen; return result; }
        std::array<std::uint8_t, 16> root{};
        first.seekg(static_cast<std::streamoff>(magic_offset + 0x14), std::ios::beg);
        first.read(reinterpret_cast<char*>(root.data()), root.size());
        if (!first) { result.error = StfsParseError::TruncatedHeader; return result; }
        const auto root_block = le32(root.data());
        const auto root_size = le32(root.data() + 4);
        if (root_size == 0 || root_size > 64u * 1024u * 1024u) {
            result.error = StfsParseError::InvalidDirectoryEntry;
            return result;
        }

        const auto read_block = [&](std::uint32_t logical_block,
                                    std::array<std::uint8_t, kBlockSize>& output) -> bool {
            const std::uint64_t start = static_cast<std::uint64_t>(result.inspection.start_data_block) * 2u;
            if (logical_block < start) return false;
            std::uint64_t true_block = logical_block - start;
            if (result.inspection.layout == SvodLayout::EnhancedGdf) true_block += 2u;
            std::uint64_t fragment_index = true_block / kBlocksPerFragment;
            const std::uint64_t file_block = true_block % kBlocksPerFragment;
            if (fragment_index >= fragments.size()) return false;
            const std::uint64_t level0_count = file_block / kBlocksPerLevel0 + 1u;
            const std::uint64_t level1_count = level0_count / kLevel0PerLevel1 + 1u;
            std::uint64_t offset = file_block * kBlockSize +
                (level0_count + level1_count) * 0x1000ull;
            if (result.inspection.layout == SvodLayout::SingleFile) offset += 0xB000ull;
            if (offset >= kFragmentPayloadSize) {
                ++fragment_index;
                offset = (offset % kFragmentPayloadSize) + 0x2000ull;
            }
            if (fragment_index >= fragments.size()) return false;
            std::ifstream data(fragments[static_cast<std::size_t>(fragment_index)], std::ios::binary);
            if (!data) return false;
            data.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
            data.read(reinterpret_cast<char*>(output.data()), output.size());
            return static_cast<bool>(data);
        };

        std::size_t visited = 0;
        std::set<std::string> unique_paths;
        std::function<bool(std::uint32_t,std::uint32_t,const std::filesystem::path&,std::size_t)> walk_directory;
        walk_directory = [&](std::uint32_t directory_block, std::uint32_t ordinal,
                             const std::filesystem::path& parent, std::size_t depth) -> bool {
            if (depth > maximum_depth || ++visited > maximum_entries) return false;
            const std::uint64_t ordinal_bytes = static_cast<std::uint64_t>(ordinal) * 4u;
            const std::uint64_t block_delta = ordinal_bytes / kBlockSize;
            const std::uint32_t in_block = static_cast<std::uint32_t>(ordinal_bytes % kBlockSize);
            if (block_delta > 0xFFFFFFFFu - directory_block) return false;
            std::array<std::uint8_t,kBlockSize> block{};
            if (!read_block(directory_block + static_cast<std::uint32_t>(block_delta), block) ||
                in_block + 14u > kBlockSize) return false;
            const auto* raw = block.data() + in_block;
            const auto left = le16(raw);
            const auto right = le16(raw + 2);
            const auto data_block = le32(raw + 4);
            const auto length = le32(raw + 8);
            const auto attributes = raw[12];
            const auto name_length = static_cast<std::size_t>(raw[13]);
            if (name_length == 0 || name_length > 255 ||
                in_block + 14u + name_length > kBlockSize) return false;
            std::string name(reinterpret_cast<const char*>(raw + 14), name_length);
            if (!safe_name(name)) return false;
            if (left && !walk_directory(directory_block, left, parent, depth + 1)) return false;
            const bool is_directory = (attributes & 0x10u) != 0;
            const auto relative = parent / name;
            if (!unique_paths.insert(relative.generic_string()).second) return false;
            result.entries.push_back({name, is_directory, data_block, length, relative});
            if (is_directory && length != 0) {
                if (!walk_directory(data_block, 0, relative, depth + 1)) return false;
            }
            if (right && !walk_directory(directory_block, right, parent, depth + 1)) return false;
            return true;
        };

        if (!walk_directory(root_block, 0, {}, 0))
            result.error = StfsParseError::InvalidDirectoryHierarchy;
        return result;
    }

    [[nodiscard]] static bool extract(
        const std::filesystem::path& package_header,
        const std::filesystem::path& destination,
        StfsParseError& error,
        std::uint64_t maximum_total_bytes = 8ull * 1024ull * 1024ull * 1024ull,
        const std::filesystem::path& fragment_directory = {}) {
        const auto parsed = SvodContainer::inspect(package_header, fragment_directory);
        if (!parsed) { error = parsed.error; return false; }
        auto fragments = parsed.fragments;
        if (fragments.empty()) {
            if (parsed.layout == SvodLayout::SingleFile) fragments.push_back(package_header);
            else {
                auto data_dir = fragment_directory;
                if (data_dir.empty()) { data_dir = package_header; data_dir += ".data"; }
                std::error_code ec;
                for (std::filesystem::directory_iterator it(data_dir, ec), end;
                     it != end && !ec; it.increment(ec))
                    if (it->is_regular_file(ec) && !ec) fragments.push_back(it->path());
                std::sort(fragments.begin(), fragments.end());
            }
        }
        const auto tree = read(package_header, fragment_directory);
        if (!tree) { error = tree.error; return false; }
        const auto read_bytes = [&](std::uint32_t first_block, std::uint32_t size,
                                    std::vector<std::uint8_t>& out) -> bool {
            if (size > 256u * 1024u * 1024u || size > maximum_total_bytes) return false;
            out.resize(size);
            std::uint64_t done = 0;
            std::uint32_t block = first_block;
            while (done < size) {
                const std::uint64_t start = static_cast<std::uint64_t>(parsed.start_data_block) * 2u;
                if (block < start) return false;
                std::uint64_t true_block = block - start;
                if (parsed.layout == SvodLayout::EnhancedGdf) true_block += 2u;
                auto fragment_index = true_block / kBlocksPerFragment;
                const auto file_block = true_block % kBlocksPerFragment;
                if (fragment_index >= fragments.size()) return false;
                const auto l0 = file_block / kBlocksPerLevel0 + 1u;
                const auto l1 = l0 / kLevel0PerLevel1 + 1u;
                std::uint64_t offset = file_block * kBlockSize + (l0 + l1) * 0x1000ull;
                if (parsed.layout == SvodLayout::SingleFile) offset += 0xB000ull;
                if (offset >= kFragmentPayloadSize) {
                    ++fragment_index;
                    offset = (offset % kFragmentPayloadSize) + 0x2000ull;
                }
                if (fragment_index >= fragments.size()) return false;
                std::ifstream in(fragments[static_cast<std::size_t>(fragment_index)], std::ios::binary);
                if (!in) return false;
                const auto amount = static_cast<std::size_t>(std::min<std::uint64_t>(kBlockSize, size - done));
                in.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
                in.read(reinterpret_cast<char*>(out.data() + done), static_cast<std::streamsize>(amount));
                if (!in) return false;
                done += amount; ++block;
            }
            return true;
        };
        std::error_code ec;
        std::filesystem::create_directories(destination, ec);
        if (ec) { error = StfsParseError::CannotOpen; return false; }
        std::uint64_t total = 0;
        for (const auto& entry : tree.entries) {
            std::filesystem::path target;
            if (!prepare_output_path(destination, entry.relative_path, entry.directory, target)) {
                error = StfsParseError::InvalidDirectoryEntry;
                return false;
            }
            if (entry.directory) continue;
            if (entry.size > maximum_total_bytes - total) {
                error = StfsParseError::InvalidDirectoryEntry; return false;
            }
            total += entry.size;
            std::vector<std::uint8_t> bytes;
            if (!read_bytes(entry.first_block, entry.size, bytes)) {
                error = StfsParseError::TruncatedHeader; return false;
            }
            std::filesystem::create_directories(target.parent_path(), ec);
            if (ec) { error = StfsParseError::CannotOpen; return false; }
            std::ofstream out(target, std::ios::binary | std::ios::trunc);
            if (!out) { error = StfsParseError::CannotOpen; return false; }
            out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if (!out) { error = StfsParseError::TruncatedHeader; return false; }
        }
        error = StfsParseError::None;
        return true;
    }

    static bool prepare_output_path(const std::filesystem::path& root,
                                    const std::filesystem::path& relative,
                                    bool directory,
                                    std::filesystem::path& target) {
        if (relative.empty() || relative.is_absolute() || relative.has_root_name())
            return false;
        std::error_code ec;
        const auto root_status = std::filesystem::symlink_status(root, ec);
        if (!ec && std::filesystem::is_symlink(root_status)) return false;
        ec.clear();
        target = root;
        std::vector<std::filesystem::path> components;
        for (const auto& component : relative) {
            if (component.empty() || component == "." || component == "..")
                return false;
            components.push_back(component);
        }
        if (components.empty()) return false;
        for (std::size_t i = 0; i < components.size(); ++i) {
            target /= components[i];
            ec.clear();
            const auto status = std::filesystem::symlink_status(target, ec);
            if (!ec && std::filesystem::is_symlink(status)) return false;
            const bool final_component = i + 1 == components.size();
            if (!final_component || directory) {
                ec.clear();
                std::filesystem::create_directories(target, ec);
                if (ec || !std::filesystem::is_directory(target, ec) || ec) return false;
            } else if (!ec && std::filesystem::is_directory(status)) {
                return false;
            }
        }
        return true;
    }

private:
    static std::uint16_t le16(const std::uint8_t* p) {
        return static_cast<std::uint16_t>(p[0] | (static_cast<std::uint16_t>(p[1]) << 8));
    }
    static std::uint32_t le32(const std::uint8_t* p) {
        return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
               (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
    }
    static bool safe_name(const std::string& name) {
        if (name.empty() || name == "." || name == ".." ||
            name.find('\\0') != std::string::npos || name.back() == '.' ||
            name.back() == ' ')
            return false;
        for (const unsigned char ch : name) {
            if (ch < 0x20u || ch == '<' || ch == '>' || ch == ':' ||
                ch == '"' || ch == '/' || ch == '\\\\' ||
                ch == '|' || ch == '?' || ch == '*')
                return false;
        }
        return true;
    }
};

} // namespace wx360::storage
