#pragma once

#include "wx360/storage/stfs_package.hpp"
#include "wx360/storage/stfs_tree.hpp"
#include "wx360/storage/svod_gdf.hpp"
#include "wx360/xex/xex_header.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

namespace wx360::storage {

// Loader helpers deliberately stop before emulation: XEX decryption,
// decompression, PE mapping, imports, and kernel startup are separate stages.
class ContentLoader {
public:
    [[nodiscard]] static std::optional<std::filesystem::path>
    find_extracted_god_default_xex(const std::filesystem::path& god_root) {
        std::error_code ec;
        if (!std::filesystem::is_directory(god_root, ec) || ec) return std::nullopt;
        std::filesystem::recursive_directory_iterator it(
            god_root, std::filesystem::directory_options::skip_permission_denied, ec);
        const std::filesystem::recursive_directory_iterator end;
        while (it != end) {
            if (ec) { ec.clear(); it.increment(ec); continue; }
            std::error_code type_error;
            if (it->is_regular_file(type_error) && !type_error) {
                auto name = it->path().filename().string();
                std::transform(name.begin(), name.end(), name.begin(),
                    [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
                if (name == "default.xex") return it->path();
            }
            it.increment(ec);
        }
        return std::nullopt;
    }

    [[nodiscard]] static bool extract_stfs_package(
        const std::filesystem::path& package,
        const std::filesystem::path& destination,
        StfsParseError& error,
        std::uint64_t maximum_total_bytes = 2ull * 1024ull * 1024ull * 1024ull) {
        const auto parsed = StfsPackageParser::parse(package);
        if (!parsed) { error = parsed.error; return false; }
        if (parsed.header.volume_type != StfsVolumeType::Stfs) {
            error = StfsParseError::UnsupportedSvodLayout;
            return false;
        }
        const auto table = StfsPackageParser::read_file_table(package, parsed.header);
        if (!table) { error = table.error; return false; }
        return StfsExtractor::extract_all(package, parsed.header, table.entries,
                                          destination, error, maximum_total_bytes);
    }

    [[nodiscard]] static std::vector<std::filesystem::path>
    find_god_data_chunks(const std::filesystem::path& god_root) {
        std::vector<std::filesystem::path> chunks;
        std::error_code ec;
        if (!std::filesystem::is_directory(god_root, ec) || ec) return chunks;
        for (std::filesystem::directory_iterator it(god_root, ec), end;
             it != end && !ec; it.increment(ec)) {
            if (!it->is_regular_file(ec) || ec) { ec.clear(); continue; }
            const auto name = it->path().filename().string();
            if (name.size() >= 8 && name.compare(0, 4, "Data") == 0 &&
                name.substr(4).find_first_not_of("0123456789") == std::string::npos)
                chunks.push_back(it->path());
        }
        std::sort(chunks.begin(), chunks.end());
        return chunks;
    }

    [[nodiscard]] static bool extract_god_svod(
        const std::filesystem::path& package_header,
        const std::filesystem::path& destination,
        StfsParseError& error,
        const std::filesystem::path& fragment_directory = {},
        std::uint64_t maximum_total_bytes = 8ull * 1024ull * 1024ull * 1024ull) {
        return SvodGdfReader::extract(package_header, destination, error,
                                      maximum_total_bytes, fragment_directory);
    }

    [[nodiscard]] static std::optional<std::filesystem::path>
    extract_god_default_xex(
        const std::filesystem::path& package_header,
        const std::filesystem::path& destination,
        StfsParseError& error,
        const std::filesystem::path& fragment_directory = {}) {
        if (!extract_god_svod(package_header, destination, error, fragment_directory))
            return std::nullopt;
        return find_extracted_god_default_xex(destination);
    }

    [[nodiscard]] static bool extract_default_xex_from_stfs(
        const std::filesystem::path& package,
        const std::filesystem::path& destination,
        StfsParseError& error) {
        const auto parsed = StfsPackageParser::parse(package);
        if (!parsed) { error = parsed.error; return false; }
        const auto table = StfsPackageParser::read_file_table(package, parsed.header);
        if (!table) { error = table.error; return false; }
        for (const auto& entry : table.entries) {
            std::string name = entry.name;
            std::transform(name.begin(), name.end(), name.begin(),
                [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            if (!entry.directory && name == "default.xex")
                return StfsPackageParser::extract_file(package, parsed.header, entry,
                                                       destination, error);
        }
        error = StfsParseError::InvalidDirectoryEntry;
        return false;
    }

};

} // namespace wx360::storage
