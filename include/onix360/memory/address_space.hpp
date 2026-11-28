#pragma once

#include "onix360/memory/physical_memory.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <limits>
#include <span>
#include <vector>

namespace onix360::memory {

// Initial software translation cache for 4 KiB guest virtual pages.
class AddressSpace {
public:
    static constexpr std::size_t page_size = PhysicalMemory::page_size;
    static constexpr std::size_t tlb_entries = 32;
    static constexpr std::size_t bat_entries = 8;
    static constexpr std::uint64_t minimum_bat_size = 128U * 1024U;
    static constexpr std::uint64_t maximum_bat_size = 256U * 1024U * 1024U;
    struct Translation { MemoryResult result{MemoryResult::Unmapped}; std::uint64_t physical_address{}; };
    struct PageMapping { std::uint64_t virtual_address{}; std::uint64_t physical_address{}; std::uint8_t permissions{}; };

    [[nodiscard]] MemoryResult map_page(std::uint64_t va, std::uint64_t pa,
                                        std::uint8_t permissions) noexcept {
        if ((va & (page_size - 1U)) || (pa & (page_size - 1U))) return MemoryResult::AlignmentFault;
        if ((permissions & 7U) == 0 || (permissions & ~7U) != 0) return MemoryResult::PermissionDenied;
        const auto vpn = va / page_size;
        for (auto& e : entries_) if (e.vpn == vpn) {
            e = {vpn, pa / page_size, permissions}; return MemoryResult::Ok;
        }
        try { entries_.push_back({vpn, pa / page_size, permissions}); }
        catch (...) { return MemoryResult::OutOfRange; }
        return MemoryResult::Ok;
    }

    [[nodiscard]] bool has_page_mapping(std::uint64_t va) const noexcept {
        if (va & (page_size - 1U)) return false;
        const auto vpn = va / page_size;
        for (const auto& entry : entries_) if (entry.vpn == vpn) return true;
        for (const auto& bat : bats_)
            if (bat.valid && va >= bat.virtual_base && va - bat.virtual_base < bat.size)
                return true;
        return false;
    }

    // Install a complete loader-produced page map atomically after validating
    // every entry. This is a software page table, not the Xenon hardware TLB.
    [[nodiscard]] MemoryResult map_pages(std::span<const PageMapping> mappings) noexcept {
        for (std::size_t i = 0; i < mappings.size(); ++i) {
            if (has_page_mapping(mappings[i].virtual_address))
                return MemoryResult::PermissionDenied;
            for (std::size_t j = 0; j < i; ++j)
                if (mappings[j].virtual_address == mappings[i].virtual_address)
                    return MemoryResult::PermissionDenied;
        }
        for (const auto& mapping : mappings) {
            if ((mapping.virtual_address & (page_size - 1U)) ||
                (mapping.physical_address & (page_size - 1U)))
                return MemoryResult::AlignmentFault;
            if ((mapping.permissions & 7U) == 0 || (mapping.permissions & ~7U) != 0)
                return MemoryResult::PermissionDenied;
        }
        try { entries_.reserve(entries_.size() + mappings.size()); }
        catch (...) { return MemoryResult::OutOfRange; }
        for (const auto& mapping : mappings) {
            const auto result = map_page(mapping.virtual_address,
                mapping.physical_address, mapping.permissions);
            if (result != MemoryResult::Ok) return result;
        }
        return MemoryResult::Ok;
    }
    // BAT-style block windows. These explicit mappings model large,
    // aligned translation regions; raw architectural BAT SPR decoding is separate.
    [[nodiscard]] MemoryResult map_bat(std::size_t index, std::uint64_t va,
                                       std::uint64_t pa, std::uint64_t size,
                                       std::uint8_t permissions) noexcept {
        if (index >= bat_entries || size < minimum_bat_size || size > maximum_bat_size ||
            (size & (size - 1U)) != 0U || (va & (size - 1U)) != 0U ||
            (pa & (size - 1U)) != 0U)
            return MemoryResult::AlignmentFault;
        if ((permissions & 7U) == 0 || (permissions & ~7U) != 0)
            return MemoryResult::PermissionDenied;
        bats_[index] = {va, pa, size, permissions, true};
        return MemoryResult::Ok;
    }
    void unmap_bat(std::size_t index) noexcept {
        if (index < bats_.size()) bats_[index].valid = false;
    }
    void unmap_page(std::uint64_t va) noexcept {
        if (va & (page_size - 1U)) return;
        const auto vpn = va / page_size;
        entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
            [vpn](const Entry& e) { return e.vpn == vpn; }), entries_.end());
    }
    void flush() noexcept {
        // A TLB invalidation must not erase persistent BAT register mappings.
        entries_.clear();
    }
    [[nodiscard]] Translation translate(std::uint64_t va, Access access,
                                        const PhysicalMemory& physical) const noexcept {
        return translate_range(va, 1, access, physical);
    }

    // Translate a complete access across one or more adjacent virtual pages.
    // A single contiguous host span is returned only when physical pages adjoin.
    [[nodiscard]] Translation translate_range(std::uint64_t va, std::size_t length,
                                               Access access,
                                               const PhysicalMemory& physical) const noexcept {
        if (length == 0 || length - 1U > std::numeric_limits<std::uint64_t>::max() - va)
            return {MemoryResult::OutOfRange, 0};
        for (const auto& bat : bats_) {
            if (!bat.valid || va < bat.virtual_base ||
                va - bat.virtual_base >= bat.size) continue;
            const std::uint64_t offset = va - bat.virtual_base;
            if (length > bat.size - offset) return {MemoryResult::Unmapped, 0};
            if ((bat.permissions & static_cast<std::uint8_t>(access)) == 0)
                return {MemoryResult::PermissionDenied, 0};
            if (offset > std::numeric_limits<std::uint64_t>::max() - bat.physical_base)
                return {MemoryResult::OutOfRange, 0};
            const auto pa = bat.physical_base + offset;
            const auto check = physical.check_access(pa, length, access);
            if (check != MemoryResult::Ok) return {check, 0};
            return {MemoryResult::Ok, pa};
        }
        std::uint64_t current_va = va;
        std::size_t remaining = length;
        std::uint64_t first_physical = 0;
        std::uint64_t expected_physical = 0;
        bool first_page = true;
        while (remaining != 0) {
            const auto offset = current_va & (page_size - 1U);
            const auto chunk = std::min<std::size_t>(
                remaining, static_cast<std::size_t>(page_size - offset));
            const auto vpn = current_va / page_size;
            const Entry* mapped = nullptr;
            for (const auto& e : entries_) {
                if (e.vpn == vpn) { mapped = &e; break; }
            }
            if (!mapped) return {MemoryResult::Unmapped, 0};
            if ((mapped->permissions & static_cast<std::uint8_t>(access)) == 0)
                return {MemoryResult::PermissionDenied, 0};
            const std::uint64_t page_physical = mapped->ppn * page_size + offset;
            if (first_page) {
                first_physical = page_physical;
                first_page = false;
            } else if (page_physical != expected_physical) {
                // A single host span can represent this access only if adjacent
                // guest pages map to adjacent physical pages.
                return {MemoryResult::Unmapped, 0};
            }
            expected_physical = page_physical + chunk;
            current_va += chunk;
            remaining -= chunk;
        }
        const auto check = physical.check_access(first_physical, length, access);
        if (check != MemoryResult::Ok) return {check, 0};
        return {MemoryResult::Ok, first_physical};
    }
private:
    struct Entry { std::uint64_t vpn{}, ppn{}; std::uint8_t permissions{}; };
    struct BatEntry { std::uint64_t virtual_base{}, physical_base{}, size{};
        std::uint8_t permissions{}; bool valid{}; };
    std::vector<Entry> entries_{};
    std::array<BatEntry, bat_entries> bats_{};
};
} // namespace onix360::memory
