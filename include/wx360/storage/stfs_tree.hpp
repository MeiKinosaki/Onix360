#pragma once

#include "wx360/storage/stfs_package.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace wx360::storage {

struct StfsTreeEntry {
    std::size_t entry_index{};
    std::size_t parent_entry_index{};
    std::filesystem::path relative_path;
    bool directory{};
};

struct StfsTreeResult {
    StfsParseError error{StfsParseError::None};
    std::vector<StfsTreeEntry> entries;
    [[nodiscard]] explicit operator bool() const noexcept {
        return error == StfsParseError::None;
    }
};

class StfsDirectoryTree {
public:
    [[nodiscard]] static StfsTreeResult build(
        const std::vector<StfsFileEntry>& entries) {
        StfsTreeResult result;
        result.entries.reserve(entries.size());
        std::vector<std::filesystem::path> paths(entries.size());
        std::vector<std::uint8_t> state(entries.size(), 0);
        for (std::size_t i = 0; i < entries.size(); ++i) {
            const auto& entry = entries[i];
            if (!safe_name(entry.name))
                return {StfsParseError::InvalidDirectoryEntry, {}};
            const auto parent = entry.parent_directory_index;
            if (parent == 0xFFFFu) {
                paths[i] = std::filesystem::path(entry.name);
                state[i] = 2;
            } else {
                if (parent >= entries.size() || parent == i ||
                    !entries[parent].directory)
                    return {StfsParseError::InvalidDirectoryHierarchy, {}};
                std::size_t cursor = parent;
                std::size_t depth = 0;
                while (cursor < entries.size() && state[cursor] != 2) {
                    if (++depth > entries.size() || state[cursor] == 1)
                        return {StfsParseError::InvalidDirectoryHierarchy, {}};
                    state[cursor] = 1;
                    const auto next = entries[cursor].parent_directory_index;
                    if (next == 0xFFFFu) break;
                    if (next >= entries.size() || !entries[next].directory)
                        return {StfsParseError::InvalidDirectoryHierarchy, {}};
                    cursor = next;
                }
                if (cursor >= entries.size())
                    return {StfsParseError::InvalidDirectoryHierarchy, {}};
                std::vector<std::size_t> chain;
                cursor = parent;
                while (state[cursor] != 2) {
                    chain.push_back(cursor);
                    const auto next = entries[cursor].parent_directory_index;
                    if (next == 0xFFFFu) {
                        paths[cursor] = entries[cursor].name;
                        state[cursor] = 2;
                        break;
                    }
                    cursor = next;
                }
                for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
                    const auto index = *it;
                    if (state[index] == 2) continue;
                    paths[index] = paths[entries[index].parent_directory_index] /
                                   entries[index].name;
                    state[index] = 2;
                }
                paths[i] = paths[parent] / entry.name;
                state[i] = 2;
            }
            result.entries.push_back({i, parent, paths[i], entry.directory});
        }
        return result;
    }

private:
    static bool safe_name(const std::string& name) {
        return !name.empty() && name != "." && name != ".." &&
               name.find_first_of("/\\:\\") == std::string::npos;
    }
};

class StfsExtractor {
public:
    [[nodiscard]] static bool extract_all(
        const std::filesystem::path& package,
        const StfsPackageHeader& header,
        const std::vector<StfsFileEntry>& files,
        const std::filesystem::path& destination,
        StfsParseError& error,
        std::uint64_t maximum_total_bytes = 2ull * 1024ull * 1024ull * 1024ull) {
        const auto tree = StfsDirectoryTree::build(files);
        if (!tree) { error = tree.error; return false; }
        std::uint64_t total = 0;
        std::error_code ec;
        std::filesystem::create_directories(destination, ec);
        if (ec) { error = StfsParseError::CannotOpen; return false; }
        for (const auto& node : tree.entries) {
            const auto target = destination / node.relative_path;
            if (node.directory) {
                std::filesystem::create_directories(target, ec);
                if (ec) { error = StfsParseError::CannotOpen; return false; }
                continue;
            }
            const auto& file = files[node.entry_index];
            if (file.file_size > maximum_total_bytes - total) {
                error = StfsParseError::InvalidDirectoryEntry;
                return false;
            }
            total += file.file_size;
            const auto bytes = StfsPackageParser::read_file(package, header, file, error);
            if (error != StfsParseError::None) return false;
            std::filesystem::create_directories(target.parent_path(), ec);
            if (ec) { error = StfsParseError::CannotOpen; return false; }
            std::ofstream out(target, std::ios::binary | std::ios::trunc);
            if (!out) { error = StfsParseError::CannotOpen; return false; }
            out.write(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
            if (!out) { error = StfsParseError::TruncatedHeader; return false; }
        }
        error = StfsParseError::None;
        return true;
    }
};

} // namespace wx360::storage
