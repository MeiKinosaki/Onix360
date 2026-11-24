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

// The kind is taken from the decoded XEX import record, never guessed from
// the ordinal. Function addresses must be registered Xenon guest trampolines;
// variable addresses must point at guest-resident storage.
enum class ImportKind : std::uint8_t { Function = 1, Variable = 2 };

struct XexImport {
    std::string library;
    std::uint16_t ordinal{};
    std::uint64_t thunk_address{};
    ImportKind kind{ImportKind::Function};
};

struct KernelExport {
    std::string library;
    std::uint16_t ordinal{};
    std::uint32_t guest_address{};
    ImportKind kind{ImportKind::Function};
};

enum class ImportResolveError : std::uint8_t {
    None, DuplicateExport, MissingExport, InvalidThunk, MemoryWriteFailed,
    KindMismatch, InvalidGuestAddress, DuplicateThunk
};

class XexImportResolver {
public:
    [[nodiscard]] bool register_export(KernelExport item) {
        if (item.library.empty() || item.guest_address == 0 ||
            (item.kind != ImportKind::Function && item.kind != ImportKind::Variable))
            return false;
        const auto key = make_key(item.library, item.ordinal);
        return exports_.emplace(key, ExportTarget{item.guest_address, item.kind}).second;
    }

    [[nodiscard]] ImportResolveError resolve(
        std::span<const XexImport> imports,
        memory::PhysicalMemory& guest_memory) const {
        struct Patch { std::uint64_t address; std::uint32_t value; };
        std::vector<Patch> patches;
        patches.reserve(imports.size());

        // Validate every import before changing guest memory. This avoids
        // partial patching when a later ordinal or thunk is invalid.
        for (std::size_t i = 0; i < imports.size(); ++i) {
            const auto& item = imports[i];
            if (item.library.empty() ||
                (item.kind != ImportKind::Function && item.kind != ImportKind::Variable))
                return ImportResolveError::MissingExport;
            const auto found = exports_.find(make_key(item.library, item.ordinal));
            if (found == exports_.end()) return ImportResolveError::MissingExport;
            if (found->second.kind != item.kind) return ImportResolveError::KindMismatch;
            if (found->second.guest_address == 0)
                return ImportResolveError::InvalidGuestAddress;
            if (guest_memory.check_access(item.thunk_address, 4, memory::Access::Write) !=
                memory::MemoryResult::Ok)
                return ImportResolveError::InvalidThunk;
            for (const auto& prior : patches)
                if (prior.address == item.thunk_address)
                    return ImportResolveError::DuplicateThunk;
            patches.push_back({item.thunk_address, found->second.guest_address});
        }

        for (const auto& patch : patches) {
            const std::uint32_t value = patch.value;
            const std::uint8_t be[4] = {
                static_cast<std::uint8_t>(value >> 24),
                static_cast<std::uint8_t>(value >> 16),
                static_cast<std::uint8_t>(value >> 8),
                static_cast<std::uint8_t>(value)};
            if (guest_memory.write(patch.address, be) != memory::MemoryResult::Ok)
                return ImportResolveError::MemoryWriteFailed;
        }
        return ImportResolveError::None;
    }

private:
    struct ExportTarget { std::uint32_t guest_address; ImportKind kind; };

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

    std::map<std::string, ExportTarget> exports_;
};

} // namespace wx360::kernel
