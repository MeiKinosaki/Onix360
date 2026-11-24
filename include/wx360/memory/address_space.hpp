#pragma once

#include "wx360/memory/physical_memory.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace wx360::memory {

// Initial software translation cache for 4 KiB guest virtual pages.
class AddressSpace {
public:
    static constexpr std::size_t page_size = PhysicalMemory::page_size;
    static constexpr std::size_t tlb_entries = 32;
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
    void unmap_page(std::uint64_t va) noexcept {
        if (va & (page_size - 1U)) return;
        const auto vpn = va / page_size;
        for (auto& e : entries_) if (e.valid && e.vpn == vpn) e.valid = false;
    }
    void flush() noexcept { for (auto& e : entries_) e.valid = false; replacement_ = 0; }
    [[nodiscard]] Translation translate(std::uint64_t va, Access access,
                                        const PhysicalMemory& physical) const noexcept {
        const auto vpn = va / page_size, offset = va & (page_size - 1U);
        for (const auto& e : entries_) {
            if (!e.valid || e.vpn != vpn) continue;
            if ((e.permissions & static_cast<std::uint8_t>(access)) == 0)
                return {MemoryResult::PermissionDenied, 0};
            const auto pa = e.ppn * page_size + offset;
            const auto check = physical.check_access(pa, 1, access);
            if (check != MemoryResult::Ok) return {check, 0};
            return {MemoryResult::Ok, pa};
        }
        return {MemoryResult::Unmapped, 0};
    }
private:
    struct Entry { std::uint64_t vpn{}, ppn{}; std::uint8_t permissions{}; bool valid{}; };
    std::array<Entry, tlb_entries> entries_{};
    std::size_t replacement_{};
};
} // namespace wx360::memory
