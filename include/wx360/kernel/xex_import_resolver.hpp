#pragma once

#include "wx360/memory/physical_memory.hpp"

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace wx360::kernel {

struct XexImport {
    std::string library;
    std::uint16_t ordinal{};
    std::uint64_t thunk_address{};
};

struct KernelExport {
    std::string library;
    std::uint16_t ordinal{};
    std::uint32_t guest_address{};
};

enum class ImportResolveError : std::uint8_t {
    None, DuplicateExport, MissingExport, InvalidThunk, MemoryWriteFailed
};

class XexImportResolver {
public:
    [[nodiscard]] bool register_export(KernelExport item) {
        const auto key = make_key(item.library, item.ordinal);
        return exports_.emplace(key, item.guest_address).second;
    }

    [[nodiscard]] ImportResolveError resolve(
        std::span<const XexImport> imports,
        memory::PhysicalMemory& guest_memory) const {
        std::vector<std::pair<std::uint64_t, std::uint32_t>> patches;
        patches.reserve(imports.size());
        for (const auto& item : imports) {
            const auto found = exports_.find(make_key(item.library, item.ordinal));
            if (found == exports_.end()) return ImportResolveError::MissingExport;
            if (item.thunk_address > guest_memory.size() ||
                4 > guest_memory.size() - item.thunk_address)
                return ImportResolveError::InvalidThunk;
            patches.emplace_back(item.thunk_address, found->second);
        }
        // Validate the complete import set before modifying guest memory.
        for (const auto& patch : patches) {
            const auto address = patch.first;
            const auto value = patch.second;
            const std::uint8_t be[4] = {
                static_cast<std::uint8_t>(value >> 24),
                static_cast<std::uint8_t>(value >> 16),
                static_cast<std::uint8_t>(value >> 8),
                static_cast<std::uint8_t>(value)};
            if (guest_memory.write(address, be) != memory::MemoryResult::Ok)
                return ImportResolveError::MemoryWriteFailed;
        }
        return ImportResolveError::None;
    }

private:
    static std::string make_key(std::string_view library, std::uint16_t ordinal) {
        std::string key;
        key.reserve(library.size() + 8);
        for (const unsigned char ch : library)
            key.push_back(static_cast<char>(ch >= 'A' && ch <= 'Z' ? ch + 32 : ch));
        key.push_back('#');
        key.push_back(static_cast<char>(ordinal >> 8));
        key.push_back(static_cast<char>(ordinal));
        return key;
    }

    std::map<std::string, std::uint32_t> exports_;
};

} // namespace wx360::kernel
