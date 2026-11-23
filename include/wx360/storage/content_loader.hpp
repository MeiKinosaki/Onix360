#pragma once

#include "wx360/storage/stfs_package.hpp"
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
