#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace wx360::storage {

enum class ContentKind { GamesOnDemand, XboxLiveArcade, DownloadableContent, TitleUpdate, StfsPackage };

struct ContentEntry {
    std::filesystem::path path;
    ContentKind kind{};
    std::string format;
};

class ContentLibrary {
public:
    [[nodiscard]] static std::vector<ContentEntry> scan(const std::filesystem::path& rom_root) {
        std::vector<ContentEntry> entries;
        std::error_code ec;
        if (!std::filesystem::is_directory(rom_root, ec) || ec) return entries;

        const auto add_directory = [&](const std::filesystem::path& directory,
                                       ContentKind kind, const char* format) {
            entries.push_back({directory, kind, format});
        };

        std::filesystem::recursive_directory_iterator it(
            rom_root, std::filesystem::directory_options::skip_permission_denied, ec);
        const std::filesystem::recursive_directory_iterator end;
        while (it != end) {
            if (ec) {
                ec.clear();
                it.increment(ec);
                continue;
            }

            const auto path = it->path();
            std::error_code type_error;
            if (it->is_directory(type_error) && !type_error) {
                const auto folder = path.filename().string();
                if (folder == "00007000") {
                    std::error_code data_error;
                    const bool has_data = std::filesystem::exists(path / "Data0000", data_error) && !data_error;
                    if (has_data) {
                        add_directory(path, ContentKind::GamesOnDemand, "GOD");
                        it.disable_recursion_pending();
                    }
                } else if (folder == "000D0000") {
                    add_directory(path, ContentKind::XboxLiveArcade, "XBLA");
                } else if (folder == "00000002") {
                    add_directory(path, ContentKind::DownloadableContent, "DLC");
                } else if (folder == "000B0000") {
                    add_directory(path, ContentKind::TitleUpdate, "Title Update");
                }
            } else if (it->is_regular_file(type_error) && !type_error) {
                auto extension = path.extension().string();
                std::transform(extension.begin(), extension.end(), extension.begin(),
                    [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
                if (extension == ".con" || extension == ".live" || extension == ".pirs") {
                    entries.push_back({path, ContentKind::StfsPackage, "STFS Package"});
                }
            }
            it.increment(ec);
        }

        std::sort(entries.begin(), entries.end(), [](const ContentEntry& a, const ContentEntry& b) {
            return a.path.generic_string() < b.path.generic_string();
        });
        return entries;
    }
};

} // namespace wx360::storage
