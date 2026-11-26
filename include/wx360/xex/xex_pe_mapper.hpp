#pragma once

#include "wx360/memory/physical_memory.hpp"
#include "wx360/memory/address_space.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace wx360::xex {

enum class PeMapError : std::uint8_t {
    None, TooSmall, BadDosSignature, BadPeSignature, WrongMachine,
    InvalidOptionalHeader, InvalidSectionTable, InvalidImageRange,
    InvalidRelocations, UnsupportedRelocation, MemoryFailure
};

struct PeMapResult {
    PeMapError error{PeMapError::None};
    std::uint32_t image_base{};
    std::uint32_t image_size{};
    std::uint32_t entry_rva{};
    std::uint64_t mapped_entry{};
    std::vector<memory::AddressSpace::PageMapping> page_mappings{};
    [[nodiscard]] explicit operator bool() const noexcept {
        return error == PeMapError::None;
    }
};

class XexPeMapper {
public:
    static constexpr std::uint16_t kPowerPcBeMachine = 0x01F2;
    static constexpr std::uint16_t kPe32Magic = 0x010B;
    static constexpr std::uint16_t kXboxSubsystem = 14;

    [[nodiscard]] static PeMapResult map(
        std::span<const std::uint8_t> image,
        memory::PhysicalMemory& memory,
        std::uint64_t physical_base,
        std::uint32_t guest_image_base,
        std::uint32_t xex_entry_point = 0) {
        PeMapResult result;
        if (image.size() < 0x40) return fail(PeMapError::TooSmall);
        if (le16(image, 0) != 0x5A4D) return fail(PeMapError::BadDosSignature);
        const auto pe_offset = le32(image, 0x3C);
        if (!range(image.size(), pe_offset, 24)) return fail(PeMapError::BadPeSignature);
        if (le32(image, pe_offset) != 0x00004550) return fail(PeMapError::BadPeSignature);
        const auto coff = static_cast<std::size_t>(pe_offset) + 4;
        if (le16(image, coff) != kPowerPcBeMachine) return fail(PeMapError::WrongMachine);
        const auto section_count = le16(image, coff + 2);
        const auto optional_size = le16(image, coff + 16);
        const auto optional = coff + 20;
        if (optional_size < 96 || !range(image.size(), optional, optional_size))
            return fail(PeMapError::InvalidOptionalHeader);
        if (le16(image, optional) != kPe32Magic ||
            le16(image, optional + 68) != kXboxSubsystem)
            return fail(PeMapError::InvalidOptionalHeader);
        const auto entry_rva = le32(image, optional + 16);
        const auto preferred_base = le32(image, optional + 28);
        const auto size_of_image = le32(image, optional + 56);
        const auto size_of_headers = le32(image, optional + 60);
        const auto directory_count = le32(image, optional + 92);
        const auto selected_entry_rva = xex_entry_point == 0 ? entry_rva :
            (xex_entry_point >= guest_image_base ? xex_entry_point - guest_image_base : 0xFFFFFFFFu);
        if (size_of_image == 0 || size_of_image > 512u * 1024u * 1024u ||
            size_of_headers == 0 || size_of_headers > image.size() || size_of_headers > size_of_image ||
            selected_entry_rva >= size_of_image)
            return fail(PeMapError::InvalidImageRange);
        const auto section_table = optional + optional_size;
        const std::uint64_t section_bytes = static_cast<std::uint64_t>(section_count) * 40u;
        if (section_count == 0 || section_count > 96 ||
            !range(image.size(), section_table, section_bytes))
            return fail(PeMapError::InvalidSectionTable);
        if (physical_base > memory.size() || size_of_image > memory.size() - physical_base ||
            guest_image_base > std::numeric_limits<std::uint32_t>::max() - size_of_image)
            return fail(PeMapError::MemoryFailure);

        // Preflight the entire physical destination before writing any image
        // bytes. A denied page must not leave earlier pages partially mapped.
        if (memory.check_access(physical_base, size_of_image,
                                memory::Access::Write) != memory::MemoryResult::Ok)
            return fail(PeMapError::MemoryFailure);

        std::vector<std::uint8_t> mapped(size_of_image, 0);
        std::copy_n(image.begin(), size_of_headers, mapped.begin());
        std::vector<std::uint8_t> page_access(
            (static_cast<std::size_t>(size_of_image) + memory::PhysicalMemory::page_size - 1) /
                memory::PhysicalMemory::page_size,
            0);
        const auto header_last_page = (static_cast<std::size_t>(size_of_headers) - 1u) /
                                      memory::PhysicalMemory::page_size;
        for (std::size_t page = 0; page <= header_last_page; ++page)
            page_access[page] |= static_cast<std::uint8_t>(memory::Access::Read);

        for (std::uint16_t i = 0; i < section_count; ++i) {
            const auto sh = section_table + static_cast<std::size_t>(i) * 40u;
            const auto virtual_size = le32(image, sh + 8);
            const auto virtual_address = le32(image, sh + 12);
            const auto raw_size = le32(image, sh + 16);
            const auto raw_offset = le32(image, sh + 20);
            const auto characteristics = le32(image, sh + 36);
            const auto copy_size = raw_size;
            if (virtual_address > size_of_image ||
                std::max(virtual_size, raw_size) > size_of_image - virtual_address ||
                !range(image.size(), raw_offset, copy_size))
                return fail(PeMapError::InvalidSectionTable);
            if (copy_size)
                std::copy_n(image.begin() + raw_offset, copy_size, mapped.begin() + virtual_address);
            const auto first_page = virtual_address / memory::PhysicalMemory::page_size;
            const auto last_page = (virtual_address + std::max(virtual_size, 1u) - 1u) /
                                   memory::PhysicalMemory::page_size;
            std::uint8_t access = 0;
            if (characteristics & 0x40000000u)
                access |= static_cast<std::uint8_t>(memory::Access::Read);
            if (characteristics & 0x80000000u)
                access |= static_cast<std::uint8_t>(memory::Access::Write);
            if (characteristics & 0x20000000u)
                access |= static_cast<std::uint8_t>(memory::Access::Execute);
            for (auto page = first_page; page <= last_page && page < page_access.size(); ++page)
                page_access[page] |= access;
        }

        // Apply PE32 HIGHLOW relocations only; all other non-ABSOLUTE forms
        // are rejected instead of silently producing a corrupt image.
        if (directory_count > 5 && optional_size >= 96 + 6 * 8) {
            const auto reloc_rva = le32(image, optional + 96 + 5 * 8);
            const auto reloc_size = le32(image, optional + 96 + 5 * 8 + 4);
            if (reloc_rva || reloc_size) {
                if (!reloc_rva || reloc_size < 8 ||
                    reloc_rva > mapped.size() || reloc_size > mapped.size() - reloc_rva)
                    return fail(PeMapError::InvalidRelocations);
                const std::int64_t delta = static_cast<std::int64_t>(guest_image_base) -
                                           static_cast<std::int64_t>(preferred_base);
                std::size_t cursor = reloc_rva;
                const std::size_t end = cursor + reloc_size;
                while (cursor < end) {
                    if (end - cursor < 8) return fail(PeMapError::InvalidRelocations);
                    const auto page_rva = le32(mapped, cursor);
                    const auto block_size = le32(mapped, cursor + 4);
                    if (block_size < 8 || block_size > end - cursor ||
                        ((block_size - 8) & 1u))
                        return fail(PeMapError::InvalidRelocations);
                    for (std::size_t off = cursor + 8; off < cursor + block_size; off += 2) {
                        const auto item = le16(mapped, off);
                        const auto type = item >> 12;
                        const auto target = static_cast<std::uint64_t>(page_rva) + (item & 0x0FFFu);
                        if (type == 0) continue;
                        if (type != 3 || target > mapped.size() || 4 > mapped.size() - target)
                            return fail(PeMapError::UnsupportedRelocation);
                        const auto old_value = le32(mapped, static_cast<std::size_t>(target));
                        const auto new_value = static_cast<std::uint32_t>(
                            static_cast<std::uint64_t>(old_value) + delta);
                        put_le32(mapped, static_cast<std::size_t>(target), new_value);
                    }
                    cursor += block_size;
                }
            }
        }

        if (memory.write(physical_base, mapped) != memory::MemoryResult::Ok)
            return fail(PeMapError::MemoryFailure);
        for (std::size_t page = 0; page < page_access.size(); ++page) {
            const auto offset = page * memory::PhysicalMemory::page_size;
            const auto length = std::min(memory::PhysicalMemory::page_size,
                                         mapped.size() - offset);
            if (memory.set_page_permissions(physical_base + offset, length,
                                            page_access[page]) != memory::MemoryResult::Ok)
                return fail(PeMapError::MemoryFailure);
        }
        result.image_base = guest_image_base;
        result.image_size = size_of_image;
        result.entry_rva = selected_entry_rva;
        result.mapped_entry = physical_base + selected_entry_rva;
        result.page_mappings.reserve(page_access.size());
        for (std::size_t page = 0; page < page_access.size(); ++page) {
            if (page_access[page] == 0) continue;
            const auto offset = page * memory::PhysicalMemory::page_size;
            result.page_mappings.push_back({
                static_cast<std::uint64_t>(guest_image_base) + offset,
                physical_base + offset,
                page_access[page]});
        }
        return result;
    }

private:
    static PeMapResult fail(PeMapError error) { return {error}; }
    static bool range(std::size_t size, std::uint64_t offset, std::uint64_t length) {
        return offset <= size && length <= size - offset;
    }
    static std::uint16_t le16(std::span<const std::uint8_t> b, std::size_t o) {
        return static_cast<std::uint16_t>(b[o] | (static_cast<std::uint16_t>(b[o + 1]) << 8));
    }
    static std::uint32_t le32(std::span<const std::uint8_t> b, std::size_t o) {
        return static_cast<std::uint32_t>(b[o]) |
            (static_cast<std::uint32_t>(b[o + 1]) << 8) |
            (static_cast<std::uint32_t>(b[o + 2]) << 16) |
            (static_cast<std::uint32_t>(b[o + 3]) << 24);
    }
    static void put_le32(std::vector<std::uint8_t>& b, std::size_t o, std::uint32_t v) {
        for (unsigned i = 0; i < 4; ++i) b[o + i] = static_cast<std::uint8_t>(v >> (8 * i));
    }
};

} // namespace wx360::xex
