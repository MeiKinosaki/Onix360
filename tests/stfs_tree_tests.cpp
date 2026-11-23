#include "wx360/storage/stfs_tree.hpp"
#include <cstdlib>
#include <iostream>
#include <vector>

int main() {
    std::vector<wx360::storage::StfsFileEntry> entries(4);
    entries[0].name = "game";
    entries[0].directory = true;
    entries[0].parent_directory_index = 0xFFFFu;
    entries[1].name = "default.xex";
    entries[1].parent_directory_index = 0;
    entries[2].name = "media";
    entries[2].directory = true;
    entries[2].parent_directory_index = 0;
    entries[3].name = "boot.bin";
    entries[3].parent_directory_index = 2;
    const auto tree = wx360::storage::StfsDirectoryTree::build(entries);
    if (!tree || tree.entries.size() != 4 ||
        tree.entries[1].relative_path.generic_string() != "game/default.xex" ||
        tree.entries[3].relative_path.generic_string() != "game/media/boot.bin") {
        std::cerr << "STFS hierarchy reconstruction failed\n";
        return EXIT_FAILURE;
    }
    entries[0].parent_directory_index = 2;
    if (wx360::storage::StfsDirectoryTree::build(entries).error !=
        wx360::storage::StfsParseError::InvalidDirectoryHierarchy) {
        std::cerr << "STFS parent cycle was not rejected\n";
        return EXIT_FAILURE;
    }
    entries[0].parent_directory_index = 0xFFFFu;
    entries[1].name = "../escape.xex";
    if (wx360::storage::StfsDirectoryTree::build(entries).error !=
        wx360::storage::StfsParseError::InvalidDirectoryEntry) {
        std::cerr << "STFS unsafe path was not rejected\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
