#include "wx360/storage/content_library.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

int main() {
    int failures = 0;
    const auto root = std::filesystem::temp_directory_path() / "wx360_content_library_test";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);

    const auto god = root / "Content" / "0000000000000000" / "12345678" / "00007000";
    const auto xbla = root / "Content" / "0000000000000000" / "12345678" / "000D0000";
    const auto dlc = root / "Content" / "0000000000000000" / "12345678" / "00000002";
    const auto tu = root / "Content" / "0000000000000000" / "12345678" / "000B0000";
    std::filesystem::create_directories(god, ec);
    std::filesystem::create_directories(xbla, ec);
    std::filesystem::create_directories(dlc, ec);
    std::filesystem::create_directories(tu, ec);
    std::filesystem::create_directories(root / "Packages", ec);

    auto touch = [](const std::filesystem::path& path) {
        std::ofstream file(path, std::ios::binary);
        file << "placeholder";
    };
    touch(god / "Data0000");
    touch(root / "Packages" / "sample.CON");
    touch(root / "Packages" / "sample.live");
    touch(root / "Packages" / "sample.pirs");
    touch(root / "Packages" / "ignore.bin");

    const auto entries = wx360::storage::ContentLibrary::scan(root);
    if (entries.size() != 7) {
        std::cerr << "Expected four content folders and three STFS package candidates; got "
                  << entries.size() << "\n";
        ++failures;
    }
    auto count = [&](const char* format) {
        int result = 0;
        for (const auto& entry : entries) if (entry.format == format) ++result;
        return result;
    };
    if (count("GOD") != 1 || count("XBLA") != 1 || count("DLC") != 1 ||
        count("Title Update") != 1 || count("STFS Package") != 3) {
        std::cerr << "Content type identification failed\n";
        ++failures;
    }
    if (!wx360::storage::ContentLibrary::scan(root / "missing").empty()) {
        std::cerr << "Missing folder should produce an empty content library\n";
        ++failures;
    }

    std::filesystem::remove_all(root, ec);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
