#pragma once

#include "onix360/storage/stfs_package.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <cctype>
#include <string>
#include <string_view>
#include <limits>
#include <vector>

namespace onix360::storage {

[[nodiscard]] inline bool parse_fragment_name(
    const std::string& filename, std::uint32_t& index) noexcept {
    // Accept canonical DataNNNN fragments and numeric NN[N...] files.
    std::string_view name(filename);
    const auto dot = name.find('.');
    if (dot != std::string_view::npos) name = name.substr(0, dot);
    if (name.size() >= 4 &&
        (name.substr(0, 4) == "Data" || name.substr(0, 4) == "data"))
        name.remove_prefix(4);
    if (name.empty()) return false;
    std::uint32_t value = 0;
    for (const char ch : name) {
        if (ch < '0' || ch > '9') return false;
        const auto digit = static_cast<std::uint32_t>(ch - '0');
        if (value > (std::numeric_limits<std::uint32_t>::max() - digit) / 10u) return false;
        value = value * 10u + digit;
    }
    index = value;
    return true;
}


enum class SvodLayout { Unknown, EnhancedGdf, Xsf, SingleFile };

struct SvodInspection {
    StfsParseError error{StfsParseError::None};
    SvodLayout layout{SvodLayout::Unknown};
    std::vector<std::filesystem::path> fragments;
    std::uint32_t declared_fragment_count{};
    std::uint32_t start_data_block{};
    std::uint32_t base_offset{};
    [[nodiscard]] explicit operator bool() const noexcept {
        return error == StfsParseError::None && layout != SvodLayout::Unknown;
    }
};

class SvodContainer {
public:
    // Inspects the SVOD descriptor and resolves the ordered physical data fragments.
    [[nodiscard]] static SvodInspection inspect(
        const std::filesystem::path& header_path,
        const std::filesystem::path& fragment_directory = {}) {
        const auto parsed = StfsPackageParser::parse(header_path);
        if (!parsed) return {parsed.error};
        if (parsed.header.volume_type != StfsVolumeType::Svod ||
            parsed.header.data_file_count == 0)
            return {StfsParseError::InvalidSvodDescriptor};
        SvodInspection result;
        result.declared_fragment_count = parsed.header.data_file_count;
        result.start_data_block = parsed.header.svod_start_data_block;
        std::filesystem::path data_dir = fragment_directory;
        if (parsed.header.data_file_count > 1 && data_dir.empty()) {
            data_dir = header_path;
            data_dir += ".data";
        }
        if (parsed.header.data_file_count > 1) {
            std::error_code ec;
            if (!std::filesystem::is_directory(data_dir, ec) || ec) {
                result.error = StfsParseError::UnsupportedSvodLayout;
                return result;
            }
            std::map<std::uint32_t, std::filesystem::path> ordered;
            for (std::filesystem::directory_iterator it(data_dir, ec), end;
                 it != end && !ec; it.increment(ec)) {
                if (!it->is_regular_file(ec) || ec) { ec.clear(); continue; }
                std::uint32_t index = 0;
                if (!parse_fragment_name(it->path().filename().string(), index) ||
                    !ordered.emplace(index, it->path()).second) {
                    result.error = StfsParseError::UnsupportedSvodLayout;
                    return result;
                }
            }
            if (ec || ordered.size() != parsed.header.data_file_count) {
                result.error = StfsParseError::UnsupportedSvodLayout;
                return result;
            }
            for (std::uint32_t index = 0; index < parsed.header.data_file_count; ++index) {
                const auto found = ordered.find(index);
                if (found == ordered.end()) {
                    result.error = StfsParseError::UnsupportedSvodLayout;
                    return result;
                }
                result.fragments.push_back(found->second);
            }
        } else {
            result.fragments.push_back(header_path);
        }
        const auto has_magic = [](const std::filesystem::path& file_path,
                                  std::uint64_t offset, const char* magic,
                                  std::size_t length) {
            std::ifstream file(file_path, std::ios::binary);
            if (!file) return false;
            std::array<char, 20> bytes{};
            if (length > bytes.size()) return false;
            file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
            file.read(bytes.data(), static_cast<std::streamsize>(length));
            return file && std::equal(bytes.begin(), bytes.begin() + length, magic);
        };
        const auto& first_fragment = result.fragments.front();
        const auto features = parsed.header.svod_device_features;
        if ((features & 0x40u) != 0) {
            if (has_magic(first_fragment, 0x2000, "MICROSOFT*XBOX*MEDIA", 20)) {
                result.layout = SvodLayout::EnhancedGdf;
                result.base_offset = 0;
            }
            else {
                result.error = StfsParseError::UnsupportedSvodLayout;
                return result;
            }
        } else if (has_magic(first_fragment, 0x12000, "MICROSOFT*XBOX*MEDIA", 20)) {
            result.layout = has_magic(first_fragment, 0x2000, "XSF", 3)
                ? SvodLayout::Xsf : SvodLayout::Unknown;
            result.base_offset = result.layout == SvodLayout::Xsf ? 0x10000u : 0u;
        } else if (has_magic(first_fragment, 0xD000, "MICROSOFT*XBOX*MEDIA", 20) &&
                   parsed.header.data_file_count == 1) {
            result.layout = SvodLayout::SingleFile;
            result.base_offset = 0xB000u;
        }
        if (result.layout == SvodLayout::Unknown)
            result.error = StfsParseError::UnsupportedSvodLayout;
        return result;
    }
};

} // namespace onix360::storage
