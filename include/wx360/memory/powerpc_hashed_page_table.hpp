#pragma once

#include "wx360/memory/physical_memory.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace wx360::memory {

// First-stage 32-bit PowerPC hashed-page-table walker for 4 KiB pages.
// The table is stored in guest physical memory as big-endian PTE0/PTE1 pairs.
// It resolves mappings only; segment-key and PP access checks are a separate stage.
class PowerPcHashedPageTable {
public:
    static constexpr std::size_t entries_per_group = 8;
    static constexpr std::size_t pte_size = 8;
    static constexpr std::size_t minimum_group_count = 1024; // 64 KiB HTAB.
    static constexpr std::size_t maximum_group_count = 524288; // 32 MiB HTAB.
    static constexpr std::uint32_t valid_bit = 0x80000000U;
    static constexpr std::uint32_t secondary_hash_bit = 0x40U;
    static constexpr std::uint32_t vsid_mask = 0x00FFFFFFU;
    static constexpr std::uint32_t rpn_mask = 0xFFFFF000U;
    // PTE1 R/C bits in the big-endian 32-bit PTE1 word.
    static constexpr std::uint32_t referenced_bit = 0x00000800U;
    static constexpr std::uint32_t changed_bit = 0x00000400U;

    struct Lookup {
        MemoryResult result{MemoryResult::Unmapped};
        std::uint64_t physical_address{};
        std::uint8_t pp{};
        bool secondary{};
        std::uint64_t pte_address{};
    };

    [[nodiscard]] MemoryResult configure(std::uint32_t physical_base,
                                         std::size_t group_count,
                                         const PhysicalMemory& ram) noexcept {
        if (group_count < minimum_group_count ||
            group_count > maximum_group_count ||
            (group_count & (group_count - 1U)) != 0U)
            return MemoryResult::AlignmentFault;
        constexpr std::size_t bytes_per_group = entries_per_group * pte_size;
        const std::size_t table_bytes = group_count * bytes_per_group;
        if ((physical_base % table_bytes) != 0U)
            return MemoryResult::AlignmentFault;
        if (physical_base > ram.size() ||
            table_bytes > ram.size() - physical_base)
            return MemoryResult::OutOfRange;
        base_ = physical_base;
        groups_ = group_count;
        configured_ = true;
        return MemoryResult::Ok;
    }

    [[nodiscard]] bool configured() const noexcept { return configured_; }
    [[nodiscard]] std::size_t group_count() const noexcept { return groups_; }

    // Decode the 32-bit OEA SDR1 layout: HTABORG[0:15], HTABMASK[23:31].
    [[nodiscard]] MemoryResult configure_from_sdr1(
        std::uint32_t sdr1, const PhysicalMemory& ram) noexcept {
        const std::uint32_t mask = sdr1 & 0x1FFU;
        unsigned extra_bits = 0;
        bool saw_zero = false;
        for (unsigned bit = 0; bit < 9; ++bit) {
            const bool set = (mask & (1U << bit)) != 0;
            if (set && saw_zero) return MemoryResult::AlignmentFault;
            if (set) ++extra_bits;
            else saw_zero = true;
        }
        const std::size_t groups = std::size_t{1} << (10U + extra_bits);
        const std::uint32_t base = sdr1 & 0xFFFF0000U;
        return configure(base, groups, ram);
    }

    [[nodiscard]] MemoryResult insert(std::uint32_t vsid,
                                      std::uint64_t effective_address,
                                      std::uint32_t physical_page,
                                      std::uint8_t pp,
                                      PhysicalMemory& ram) const noexcept {
        if (!configured_) return MemoryResult::Unmapped;
        if ((physical_page & 0xFFFU) != 0U || pp > 3U)
            return MemoryResult::AlignmentFault;
        const auto primary = page_hash(vsid, effective_address);
        auto status = insert_in_group(primary, false, vsid, effective_address,
                                       physical_page, pp, ram);
        if (status == MemoryResult::Ok) return status;
        if (status != MemoryResult::Unmapped) return status;
        return insert_in_group(~primary, true, vsid, effective_address,
                               physical_page, pp, ram);
    }

    [[nodiscard]] Lookup lookup(std::uint32_t vsid,
                                std::uint64_t effective_address,
                                const PhysicalMemory& ram) const noexcept {
        if (!configured_) return {MemoryResult::Unmapped};
        const auto primary = page_hash(vsid, effective_address);
        auto found = lookup_group(primary, false, vsid, effective_address, ram);
        if (found.result == MemoryResult::Ok) return found;
        if (found.result != MemoryResult::Unmapped) return found;
        return lookup_group(~primary, true, vsid, effective_address, ram);
    }

    [[nodiscard]] static constexpr std::uint32_t page_hash(
        std::uint32_t vsid, std::uint64_t effective_address) noexcept {
        const auto page_index =
            static_cast<std::uint32_t>((effective_address >> 12U) & 0xFFFFU);
        return (vsid & vsid_mask) ^ page_index;
    }

private:
    [[nodiscard]] std::uint64_t group_address(std::uint32_t hash) const noexcept {
        const auto group = static_cast<std::size_t>(hash) & (groups_ - 1U);
        return static_cast<std::uint64_t>(base_) +
               group * entries_per_group * pte_size;
    }

    [[nodiscard]] static std::uint32_t make_pte0(
        std::uint32_t vsid, std::uint64_t ea, bool secondary) noexcept {
        const auto api = static_cast<std::uint32_t>((ea >> 22U) & 0x3FU);
        return valid_bit | ((vsid & vsid_mask) << 7U) |
               (secondary ? secondary_hash_bit : 0U) | api;
    }

    [[nodiscard]] static std::uint32_t make_pte1(
        std::uint32_t physical_page, std::uint8_t pp) noexcept {
        return (physical_page & rpn_mask) | (pp & 3U);
    }

    [[nodiscard]] static bool read_word(const PhysicalMemory& ram,
                                        std::uint64_t address,
                                        std::uint32_t& value) noexcept {
        std::array<std::uint8_t,4> bytes{};
        if (ram.read(address, bytes) != MemoryResult::Ok) return false;
        value = (static_cast<std::uint32_t>(bytes[0]) << 24U) |
                (static_cast<std::uint32_t>(bytes[1]) << 16U) |
                (static_cast<std::uint32_t>(bytes[2]) << 8U) | bytes[3];
        return true;
    }

    [[nodiscard]] static bool write_word(PhysicalMemory& ram,
                                         std::uint64_t address,
                                         std::uint32_t value) noexcept {
        const std::array<std::uint8_t,4> bytes{
            static_cast<std::uint8_t>(value >> 24U),
            static_cast<std::uint8_t>(value >> 16U),
            static_cast<std::uint8_t>(value >> 8U),
            static_cast<std::uint8_t>(value)};
        return ram.write(address, bytes) == MemoryResult::Ok;
    }

    [[nodiscard]] MemoryResult insert_in_group(
        std::uint32_t hash, bool secondary, std::uint32_t vsid,
        std::uint64_t ea, std::uint32_t physical_page, std::uint8_t pp,
        PhysicalMemory& ram) const noexcept {
        const auto first = group_address(hash);
        for (std::size_t i = 0; i < entries_per_group; ++i) {
            const auto pte = first + i * pte_size;
            std::uint32_t pte0{};
            if (!read_word(ram, pte, pte0)) return MemoryResult::OutOfRange;
            if ((pte0 & valid_bit) != 0U) continue;
            if (!write_word(ram, pte + 4U, make_pte1(physical_page, pp)) ||
                !write_word(ram, pte, make_pte0(vsid, ea, secondary)))
                return MemoryResult::OutOfRange;
            return MemoryResult::Ok;
        }
        return MemoryResult::Unmapped;
    }

    [[nodiscard]] Lookup lookup_group(
        std::uint32_t hash, bool secondary, std::uint32_t vsid,
        std::uint64_t ea, const PhysicalMemory& ram) const noexcept {
        const auto first = group_address(hash);
        const auto expected = make_pte0(vsid, ea, secondary);
        for (std::size_t i = 0; i < entries_per_group; ++i) {
            const auto pte = first + i * pte_size;
            std::uint32_t pte0{}, pte1{};
            if (!read_word(ram, pte, pte0) ||
                !read_word(ram, pte + 4U, pte1))
                return {MemoryResult::OutOfRange};
            if ((pte0 & (valid_bit | (vsid_mask << 7U) |
                         secondary_hash_bit | 0x3FU)) != expected)
                continue;
            const auto page_offset = effective_page_offset(ea);
            // PTE1 reference/change bits are set by the memory access path.
            return {MemoryResult::Ok,
                    static_cast<std::uint64_t>(pte1 & rpn_mask) + page_offset,
                    static_cast<std::uint8_t>(pte1 & 3U), secondary, pte};
        }
        return {MemoryResult::Unmapped};
    }

    [[nodiscard]] static constexpr std::uint64_t effective_page_offset(
        std::uint64_t ea) noexcept { return ea & 0xFFFU; }

    std::uint32_t base_{};
    std::size_t groups_{};
    bool configured_{};
};

} // namespace wx360::memory
