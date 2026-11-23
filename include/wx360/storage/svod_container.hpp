#pragma once

#include "wx360/storage/stfs_package.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace wx360::storage {

enum class SvodLayout { Unknown, EnhancedGdf, Xsf, SingleFile };

struct SvodInspection {
    StfsParseError error{StfsParseError::None};
    SvodLayout layout{SvodLayout::Unknown};
    std::vector<std::filesystem::path> fragments;
    std::uint32_t declared_fragment_count{};
    std::uint32_t start_data_block{};
    [[nodiscard]] explicit operator bool() const noexcept {
        return error == StfsParseError::None && layout != SvodLayout::Unknown;
    }
};

class SvodContainer {
public:
    // Inspects the SVOD descriptor and physical layout markers. It does not
    // yet traverse the SVOD/GDF directory tree or mount its file extents.
    [[nodiscard]] static SvodInspection inspect(
        const std::filesystem::path& header_path,
        const std::filesystem::path& fragment_directory = {}) {
        const auto parsed = StfsPackageParser::parse(header_path);
        if (!parsed) return {parsed.error};
        if (parsed.header.volume_type != StfsVolumeType::Svod)
            return {StfsParseError::InvalidSvodDescriptor};
        SvodInspection result;
        result.declared_fragment_count = parsed.header.data_file_count;
        result.start_data_block = parsed.header.svod_start_data_block;
        if (!fragment_directory.empty()) {
            std::error_code ec;
            if (std::filesystem::is_directory(fragment_directory, ec) && !ec) {
                for (std::filesystem::directory_iterator it(fragment_directory, ec), end;
                     it != end && !ec; it.increment(ec)) {
                    if (it->is_regular_file(ec) && !ec)
                        result.fragments.push_back(it->path());
                    ec.clear();
                }
                std::sort(result.fragments.begin(), result.fragments.end());
                if (result.declared_fragment_count != 0 &&
                    result.fragments.size() != result.declared_fragment_count) {
                    result.error = StfsParseError::UnsupportedSvodLayout;
                    return result;
                }
            }
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
        const auto features = parsed.header.svod_device_features;
        if ((features & 0x01u) != 0 &&
            has_magic(header_path, 0x2000, "MICROSOFT*XBOX*MEDIA", 20)) {
            result.layout = SvodLayout::EnhancedGdf;
        } else if (has_magic(header_path, 0x12000, "MICROSOFT*XBOX*MEDIA", 20)) {
            result.layout = has_magic(header_path, 0x2000, "XSF", 3)
                ? SvodLayout::Xsf : SvodLayout::Unknown;
        } else if (has_magic(header_path, 0xD000, "MICROSOFT*XBOX*MEDIA", 20) &&
                   parsed.header.data_file_count == 1) {
            result.layout = SvodLayout::SingleFile;
        }
        if (result.layout == SvodLayout::Unknown)
            result.error = StfsParseError::UnsupportedSvodLayout;
        return result;
    }
};

} // namespace wx360::storage
