#pragma once

#include "wx360/memory/physical_memory.hpp"

#include <array>
#include <algorithm>
#include <cstddef>
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
    // Address of the 32-bit XEX metadata record; never a patch destination.
    std::uint64_t value_address{};
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
    [[nodiscard]] bool has_export(std::string_view library, std::uint16_t ordinal) const {
        return exports_.find(make_key(library, ordinal)) != exports_.end();
    }

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
        struct Patch {
            std::uint64_t address{};
            std::array<std::uint8_t, 16> bytes{};
            std::size_t size{};
        };
        std::vector<Patch> patches;
        patches.reserve(imports.size() * 2U);

        const auto append_patch = [&](std::uint64_t address,
                                      std::span<const std::uint8_t> bytes,
                                      std::vector<Patch>& staged) -> bool {
            if (address == 0 || bytes.empty() || bytes.size() > 16 ||
                guest_memory.check_access(address, bytes.size(), memory::Access::Write) !=
                    memory::MemoryResult::Ok)
                return false;
            const auto end = address + bytes.size();
            for (const auto& prior : staged) {
                const auto prior_end = prior.address + prior.size;
                if (address < prior_end && prior.address < end) return false;
            }
            Patch patch{};
            patch.address = address;
            patch.size = bytes.size();
            std::copy(bytes.begin(), bytes.end(), patch.bytes.begin());
            staged.push_back(patch);
            return true;
        };
        const auto encode_be32 = [](std::uint32_t value) {
            return std::array<std::uint8_t, 4>{
                static_cast<std::uint8_t>(value >> 24U),
                static_cast<std::uint8_t>(value >> 16U),
                static_cast<std::uint8_t>(value >> 8U),
                static_cast<std::uint8_t>(value)};
        };

        // Stage both the import value slot and executable function thunk.
        // No guest bytes are changed unless every import in the batch validates.
        for (const auto& item : imports) {
            if (item.library.empty() ||
                (item.kind != ImportKind::Function && item.kind != ImportKind::Variable))
                return ImportResolveError::MissingExport;
            const auto found = exports_.find(make_key(item.library, item.ordinal));
            if (found == exports_.end()) return ImportResolveError::MissingExport;
            if (found->second.kind != item.kind) return ImportResolveError::KindMismatch;

            if (item.kind == ImportKind::Function) {
                if (item.thunk_address == 0 ||
                    guest_memory.check_access(item.thunk_address, 16, memory::Access::Write) !=
                        memory::MemoryResult::Ok)
                    return ImportResolveError::InvalidThunk;

                // Match Xenia's reverse-engineered kernel import thunk:
                // sc with LEV=2, blr, nop, nop. The runtime dispatches this
                // special call by the guest thunk PC, not by an invented
                // direct branch to a host address.
                const std::array<std::uint32_t, 4> code{
                    0x44000042U, // sc 2 (WX360 kernel-import dispatch)
                    0x4E800020U, // blr
                    0x60000000U, // nop
                    0x60000000U  // nop
                };
                std::array<std::uint8_t, 16> thunk_code{};
                for (std::size_t word = 0; word < code.size(); ++word) {
                    const auto encoded = encode_be32(code[word]);
                    std::copy(encoded.begin(), encoded.end(),
                              thunk_code.begin() + static_cast<std::ptrdiff_t>(word * 4U));
                }
                if (!append_patch(item.thunk_address, thunk_code, patches))
                    return ImportResolveError::DuplicateThunk;
                // Type-0 function descriptor is metadata, not an IAT
                // pointer. The reverse-engineered XEX loader marks it with
                // 0xDEADC0DE; the type-1 record is the callable thunk.
                const auto descriptor = encode_be32(0xDEADC0DEU);
                if (item.value_address == 0 ||
                    guest_memory.check_access(item.value_address, descriptor.size(),
                        memory::Access::Write) != memory::MemoryResult::Ok)
                    return ImportResolveError::InvalidThunk;
                if (!append_patch(item.value_address, descriptor, patches))
                    return ImportResolveError::DuplicateThunk;
            } else {
                const auto slot = encode_be32(found->second.guest_address);
                if (item.value_address == 0 ||
                    guest_memory.check_access(item.value_address, slot.size(),
                        memory::Access::Write) != memory::MemoryResult::Ok)
                    return ImportResolveError::InvalidThunk;
                if (!append_patch(item.value_address, slot, patches))
                    return ImportResolveError::DuplicateThunk;
            }
        }

        for (const auto& patch : patches) {
            if (guest_memory.write(patch.address,
                    std::span<const std::uint8_t>(patch.bytes.data(), patch.size)) !=
                memory::MemoryResult::Ok)
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
