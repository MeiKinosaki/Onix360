#pragma once

#include "wx360/xex/xex_header.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <utility>

namespace wx360::xex {

enum class LoadError : std::uint8_t {
    None,
    CannotOpen,
    InvalidFileSize,
    HeaderReadFailure,
    InvalidHeader,
    ImageNotMapped,
};

struct LaunchPlan {
    Header header{};
    std::filesystem::path source;
    std::uint64_t file_size{};
    std::uint32_t entry_point{};
    bool requires_decryption{};
    bool requires_decompression{};
    bool requires_pe_mapping{true};
    bool kernel_imports_resolved{};
};

struct LoadResult {
    LoadError error{LoadError::None};
    LaunchPlan plan{};
    [[nodiscard]] explicit operator bool() const noexcept {
        return error == LoadError::None;
    }
};

class XexImageLoader {
public:
    static constexpr std::size_t kHeaderWindow = 0x10000;
    static constexpr std::uint64_t kMaximumImageSize = 2ull * 1024ull * 1024ull * 1024ull;

    // Inspects XEX2 metadata and prepares a launch plan. It deliberately does
    // not claim to decrypt, decompress, map PE sections or resolve imports.
    [[nodiscard]] static LoadResult inspect(const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) return {LoadError::CannotOpen, {}};
        file.seekg(0, std::ios::end);
        const auto end = file.tellg();
        if (end < 24) return {LoadError::InvalidFileSize, {}};
        const auto size = static_cast<std::uint64_t>(end);
        if (size > kMaximumImageSize) return {LoadError::InvalidFileSize, {}};
        const auto window_size = static_cast<std::size_t>(
            std::min<std::uint64_t>(size, kHeaderWindow));
        std::array<std::uint8_t, kHeaderWindow> window{};
        file.seekg(0, std::ios::beg);
        file.read(reinterpret_cast<char*>(window.data()),
                  static_cast<std::streamsize>(window_size));
        if (static_cast<std::size_t>(file.gcount()) != window_size)
            return {LoadError::HeaderReadFailure, {}};
        Header header{};
        if (parse_header(std::span<const std::uint8_t>(window.data(), window_size),
                         header, size) != ParseResult::Ok)
            return {LoadError::InvalidHeader, {}};
        LaunchPlan plan{};
        plan.header = header;
        plan.source = path;
        plan.file_size = size;
        plan.entry_point = header.entry_point;
        return {LoadError::None, std::move(plan)};
    }
};

} // namespace wx360::xex
