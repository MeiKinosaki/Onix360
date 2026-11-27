#pragma once

#include "wx360/memory/physical_memory.hpp"
#include "wx360/memory/address_space.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>

namespace wx360::kernel {

// Deterministic guest-physical heap for early xboxkrnl memory services.
// The heap owns only its configured RAM interval; it never grows host RAM.
class KernelHeap {
public:
    KernelHeap(memory::PhysicalMemory& ram, std::uint32_t base,
               std::uint32_t size) noexcept
        : ram_(&ram), base_(base), size_(size) {
        if (size == 0 || base > ram.size() ||
            size > ram.size() - static_cast<std::size_t>(base)) {
            valid_ = false;
            return;
        }
        free_.emplace(base, size);
        valid_ = true;
    }

    [[nodiscard]] bool valid() const noexcept { return valid_; }
    [[nodiscard]] std::uint32_t base() const noexcept { return base_; }
    [[nodiscard]] std::uint32_t size() const noexcept { return size_; }
    [[nodiscard]] std::size_t allocation_count() const noexcept {
        return allocated_.size();
    }
    [[nodiscard]] std::uint64_t free_bytes() const noexcept {
        std::uint64_t total = 0;
        for (const auto& [address, bytes] : free_) {
            (void)address;
            total += bytes;
        }
        return total;
    }
    [[nodiscard]] std::uint32_t largest_free_block() const noexcept {
        std::uint32_t largest = 0;
        for (const auto& [address, bytes] : free_) {
            (void)address;
            largest = (std::max)(largest, bytes);
        }
        return largest;
    }
    [[nodiscard]] bool owns_address(std::uint32_t address) const noexcept {
        return valid_ && address >= base_ &&
            static_cast<std::uint64_t>(address) <
                static_cast<std::uint64_t>(base_) + size_;
    }

    // Expensive diagnostic used by tests and debug builds. The free and
    // allocated maps must partition the configured heap without overlap,
    // gaps, out-of-range blocks, or adjacent unmerged free blocks.
    [[nodiscard]] bool validate() const noexcept {
        if (!valid_) return false;
        std::uint64_t total = 0;
        std::uint64_t previous_end = base_;
        bool first_block = true;
        bool previous_was_free = false;
        auto free_it = free_.begin();
        auto allocated_it = allocated_.begin();
        while (free_it != free_.end() || allocated_it != allocated_.end()) {
            const bool take_free = allocated_it == allocated_.end() ||
                (free_it != free_.end() && free_it->first < allocated_it->first);
            const std::uint64_t start = take_free ? free_it->first : allocated_it->first;
            const std::uint64_t bytes = take_free ? free_it->second : allocated_it->second;
            if (bytes == 0 || start < base_ ||
                start + bytes > static_cast<std::uint64_t>(base_) + size_ ||
                (!first_block && start != previous_end))
                return false;
            if (take_free && !first_block && previous_was_free) return false;
            previous_end = start + bytes;
            total += bytes;
            first_block = false;
            previous_was_free = take_free;
            if (take_free) ++free_it;
            else ++allocated_it;
        }
        return total == size_ && previous_end ==
            static_cast<std::uint64_t>(base_) + size_;
    }

    // Alignment must be a power of two. Allocation is first-fit and returns
    // a guest physical address; zero is never returned as a valid block.
    [[nodiscard]] std::optional<std::uint32_t> allocate(
        std::uint32_t bytes, std::uint32_t alignment = 16) {
        if (!valid_ || bytes == 0 || alignment == 0 ||
            (alignment & (alignment - 1U)) != 0)
            return std::nullopt;

        for (auto it = free_.begin(); it != free_.end(); ++it) {
            const std::uint64_t start = it->first;
            const std::uint64_t end = start + it->second;
            const std::uint64_t aligned =
                (start + alignment - 1U) & ~static_cast<std::uint64_t>(alignment - 1U);
            if (aligned > end || bytes > end - aligned ||
                aligned > std::numeric_limits<std::uint32_t>::max())
                continue;

            const auto block_start = it->first;
            const auto block_size = it->second;
            free_.erase(it);
            if (aligned > block_start)
                free_.emplace(block_start, static_cast<std::uint32_t>(aligned - block_start));
            const auto allocation_end = aligned + bytes;
            if (allocation_end < static_cast<std::uint64_t>(block_start) + block_size)
                free_.emplace(static_cast<std::uint32_t>(allocation_end),
                    static_cast<std::uint32_t>(static_cast<std::uint64_t>(block_start) +
                                               block_size - allocation_end));
            const auto address = static_cast<std::uint32_t>(aligned);
            allocated_.emplace(address, bytes);
            return address;
        }
        return std::nullopt;
    }

    // Allocate page-granular backing and install a guest virtual mapping.
    // Mapping is committed only after the physical allocation succeeds;
    // a failed map rolls the allocation back.
    [[nodiscard]] std::optional<std::uint32_t> allocate_mapped_pages(
        memory::AddressSpace& address_space, std::uint64_t virtual_base,
        std::uint32_t bytes, std::uint8_t permissions = 3U) {
        if (bytes == 0 || (bytes % memory::PhysicalMemory::page_size) != 0 ||
            (virtual_base % memory::PhysicalMemory::page_size) != 0)
            return std::nullopt;
        const auto physical = allocate(bytes,
            static_cast<std::uint32_t>(memory::PhysicalMemory::page_size));
        if (!physical) return std::nullopt;
        std::vector<memory::AddressSpace::PageMapping> mappings;
        try {
            mappings.reserve(bytes / memory::PhysicalMemory::page_size);
            for (std::uint32_t offset = 0; offset < bytes;
                 offset += static_cast<std::uint32_t>(memory::PhysicalMemory::page_size))
                mappings.push_back({virtual_base + offset,
                    static_cast<std::uint64_t>(*physical) + offset, permissions});
        } catch (...) {
            (void)release(*physical);
            return std::nullopt;
        }
        if (address_space.map_pages(mappings) != memory::MemoryResult::Ok) {
            (void)release(*physical);
            return std::nullopt;
        }
        return physical;
    }

    // Unmap all virtual pages before returning backing memory to the heap.
    [[nodiscard]] bool release_mapped_pages(memory::AddressSpace& address_space,
        std::uint64_t virtual_base, std::uint32_t physical_base,
        std::uint32_t bytes) {
        if (bytes == 0 || (bytes % memory::PhysicalMemory::page_size) != 0 ||
            (virtual_base % memory::PhysicalMemory::page_size) != 0 ||
            !allocated_.contains(physical_base) || allocated_.at(physical_base) != bytes)
            return false;
        for (std::uint32_t offset = 0; offset < bytes;
             offset += static_cast<std::uint32_t>(memory::PhysicalMemory::page_size))
            if (!address_space.has_page_mapping(virtual_base + offset)) return false;
        for (std::uint32_t offset = 0; offset < bytes;
             offset += static_cast<std::uint32_t>(memory::PhysicalMemory::page_size))
            address_space.unmap_page(virtual_base + offset);
        return release(physical_base);
    }

    // Only the exact base of a live allocation may be released.
    [[nodiscard]] bool release(std::uint32_t address) {
        if (!valid_) return false;
        const auto allocation = allocated_.find(address);
        if (allocation == allocated_.end()) return false;
        const auto bytes = allocation->second;
        allocated_.erase(allocation);

        auto next = free_.lower_bound(address);
        auto previous = next;
        bool has_previous = previous != free_.begin();
        if (has_previous) --previous;

        std::uint32_t merged_start = address;
        std::uint64_t merged_size = bytes;
        if (has_previous &&
            static_cast<std::uint64_t>(previous->first) + previous->second == address) {
            merged_start = previous->first;
            merged_size += previous->second;
            free_.erase(previous);
        }
        next = free_.lower_bound(address);
        if (next != free_.end() && merged_size + merged_start == next->first) {
            merged_size += next->second;
            free_.erase(next);
        }
        if (merged_size > std::numeric_limits<std::uint32_t>::max())
            return false;
        free_.emplace(merged_start, static_cast<std::uint32_t>(merged_size));
        return true;
    }

private:
    memory::PhysicalMemory* ram_{};
    std::uint32_t base_{};
    std::uint32_t size_{};
    bool valid_{};
    std::map<std::uint32_t, std::uint32_t> free_;
    std::map<std::uint32_t, std::uint32_t> allocated_;
};

} // namespace wx360::kernel
