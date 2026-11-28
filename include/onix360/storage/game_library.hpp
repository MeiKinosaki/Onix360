#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>
#include "onix360/xex/xex_image_loader.hpp"

namespace onix360::storage {

struct GameFile {
    std::filesystem::path path;
    std::string format;
};

class GameLibrary {
public:
    // Opens a library entry through the full metadata reader. This is inspection
    // only: it never maps guest memory or starts a Xenon execution session.
    [[nodiscard]] static xex::LoadError open_xex(
        const GameFile& game, xex::FullXexImage& output) {
        auto extension = game.path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (game.format != "XEX" || extension != ".xex")
            return xex::LoadError::InvalidHeader;
        return xex::XexImageLoader::read_complete(game.path, output);
    }

    [[nodiscard]] static std::vector<GameFile> scan(
        const std::filesystem::path& root) {
        std::vector<GameFile> games;
        std::error_code ec;
        if (!std::filesystem::is_directory(root, ec) || ec) return games;

        std::filesystem::recursive_directory_iterator it(
            root, std::filesystem::directory_options::skip_permission_denied, ec);
        const std::filesystem::recursive_directory_iterator end;
        while (it != end) {
            if (ec) {
                ec.clear();
                it.increment(ec);
                continue;
            }
            std::error_code type_error;
            if (it->is_regular_file(type_error) && !type_error) {
                auto extension = it->path().extension().string();
                std::transform(extension.begin(), extension.end(), extension.begin(),
                    [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
                if (extension == ".iso" || extension == ".xex") {
                    games.push_back({it->path(), extension == ".iso" ? "ISO" : "XEX"});
                }
            }
            it.increment(ec);
        }
        std::sort(games.begin(), games.end(),
            [](const GameFile& a, const GameFile& b) {
                return a.path.filename().string() < b.path.filename().string();
            });
        return games;
    }
};

} // namespace onix360::storage
