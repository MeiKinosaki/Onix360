#include "onix360/storage/game_library.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

int main() {
    int failures = 0;
    const auto root = std::filesystem::temp_directory_path() / "onix360_library_test";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / "nested", ec);
    auto touch = [](const std::filesystem::path& path) {
        std::ofstream file(path, std::ios::binary);
        file << "test";
    };
    touch(root / "alpha.iso");
    touch(root / "nested" / "beta.XEX");
    touch(root / "ignore.txt");

    const auto games = onix360::storage::GameLibrary::scan(root);
    if (games.size() != 2) { std::cerr << "Expected two supported games\n"; ++failures; }
    if (games.size() == 2) {
        if (games[0].format != "ISO" || games[1].format != "XEX") {
            std::cerr << "Formats were not detected correctly\n"; ++failures;
        }
    }
    if (!onix360::storage::GameLibrary::scan(root / "missing").empty()) {
        std::cerr << "Missing folder should produce an empty library\n"; ++failures;
    }
    std::filesystem::remove_all(root, ec);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
