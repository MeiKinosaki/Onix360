#pragma once

#include "wx360/memory/physical_memory.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <limits>

namespace wx360::memory {

// Initial software translation cache for 4 KiB guest virtual pages.
class AddressSpace {
public:
    static constexpr std::size_t page_size = PhysicalMemory::page_size;
    static constexpr std::size_t tlb_entries = 32;
    static constexpr std::size_t bat_entries = 8;
    static constexpr std::uint64_t minimum_bat_size = 128U * 1024U;
    static constexpr std::uint64_t maximum_bat_size = 256U * 1024U * 1024U;
    struct Translation { MemoryResult result{MemoryResult::Unmapped}; std::uint64_t physical_address{}; };

    [[nodiscard]] MemoryResult map_page(std::uint64_t va, std::uint64_t pa,
                                        std::uint8_t permissions) noexcept {
        if ((va & (page_size - 1U)) || (pa & (page_size - 1U))) return MemoryResult::AlignmentFault;
        if ((permissions & 7U) == 0 || (permissions & ~7U) != 0) return MemoryResult::PermissionDenied;
        const auto vpn = va / page_size;
        for (auto& e : entries_) if (e.valid && e.vpn == vpn) {
            e = {vpn, pa / page_size, permissions, true}; return MemoryResult::Ok;
        }
        entries_[replacement_++ % entries_.size()] = {vpn, pa / page_size, permissions, true};
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
        for (auto& e : entries_) if (e.valid && e.vpn == vpn) e.valid = false;
    }
    void flush() noexcept {
        for (auto& e : entries_) e.valid = false;
        for (auto& bat : bats_) bat.valid = false;
        replacement_ = 0;
    }
    [[nodiscard]] Translation translate(std::uint64_t va, Access access,
                                        const PhysicalMemory& physical) const noexcept {
        return translate_range(va, 1, access, physical);
    }

    // Translate a complete access, not just its first byte. This initial
    // page-table model deliberately rejects accesses crossing a 4 KiB page;
    // callers can split them once multi-page architectural access handling exists.
    [[nodiscard]] Translation translate_range(std::uint64_t va, std::size_t length,
                                               Access access,
                                               const PhysicalMemory& physical) const noexcept {
        if (length == 0) return {MemoryResult::OutOfRange, 0};
        for (const auto& bat : bats_) {
            if (!bat.valid || va < bat.virtual_base ||
                va - bat.virtual_base >= bat.size) continue;
            const std::uint64_t offset = va - bat.virtual_base;
            if (length > bat.size - offset) return {MemoryResult::Unmapped, 0};
            if ((bat.permissions & static_cast<std::uint8_t>(access)) == 0)
                return {MemoryResult::PermissionDenied, 0};
            const auto pa = bat.physical_base + offset;
            const auto check = physical.check_access(pa, length, access);
            if (check != MemoryResult::Ok) return {check, 0};
            return {MemoryResult::Ok, pa};
        }
        if (length - 1U > std::numeric_limits<std::uint64_t>::max() - va) return {MemoryResult::OutOfRange, 0};
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
                if (e.valid && e.vpn == vpn) { mapped = &e; break; }
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
    struct Entry { std::uint64_t vpn{}, ppn{}; std::uint8_t permissions{}; bool valid{}; };
    struct BatEntry { std::uint64_t virtual_base{}, physical_base{}, size{};
        std::uint8_t permissions{}; bool valid{}; };
    std::array<Entry, tlb_entries> entries_{};
    std::array<BatEntry, bat_entries> bats_{};
    std::size_t replacement_{};
};
} // namespace wx360::memory
