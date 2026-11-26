#include "wx360/xex/xex_pe_mapper.hpp"

#include <cstdlib>
#include <cstdint>
#include <array>
#include <iostream>
#include <vector>

static void put16(std::vector<std::uint8_t>& b, std::size_t o, std::uint16_t v) {
    b[o] = static_cast<std::uint8_t>(v); b[o + 1] = static_cast<std::uint8_t>(v >> 8);
}
static void put32(std::vector<std::uint8_t>& b, std::size_t o, std::uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) b[o + i] = static_cast<std::uint8_t>(v >> (8 * i));
}

int main() {
    std::vector<std::uint8_t> pe(0x400, 0);
    put16(pe, 0, 0x5A4D);
    put32(pe, 0x3C, 0x80);
    put32(pe, 0x80, 0x00004550);
    put16(pe, 0x84, 0x01F2); // PowerPC big-endian
    put16(pe, 0x86, 1);
    put16(pe, 0x94, 0xE0);
    const std::size_t opt = 0x98;
    put16(pe, opt, 0x010B);
    put32(pe, opt + 16, 0x1000);
    put32(pe, opt + 28, 0x82000000);
    put32(pe, opt + 56, 0x2000);
    put32(pe, opt + 60, 0x200);
    put16(pe, opt + 68, 14);
    put32(pe, opt + 92, 16);
    const std::size_t sh = opt + 0xE0;
    pe[sh] = '.'; pe[sh + 1] = 't'; pe[sh + 2] = 'e'; pe[sh + 3] = 'x'; pe[sh + 4] = 't';
    put32(pe, sh + 8, 0x100);
    put32(pe, sh + 12, 0x1000);
    put32(pe, sh + 16, 0x200);
    put32(pe, sh + 20, 0x200);
    put32(pe, sh + 36, 0x60000020); // code, read, execute
    pe[0x200] = 0x38; pe[0x201] = 0x60; pe[0x202] = 0; pe[0x203] = 42;

    wx360::memory::PhysicalMemory ram(0x10000);
    const auto result = wx360::xex::XexPeMapper::map(pe, ram, 0x2000, 0x82000000);
    if (!result || result.mapped_entry != 0x3000 || result.image_size != 0x2000) {
        std::cerr << "PE map failed: " << static_cast<int>(result.error) << "\n";
        return EXIT_FAILURE;
    }
    std::uint32_t instruction = 0;
    if (ram.fetch_u32(0x3000, instruction) != wx360::memory::MemoryResult::Ok ||
        instruction != 0x3860002A) {
        std::cerr << "Mapped executable section contents/permissions invalid\n";
        return EXIT_FAILURE;
    }
    if (ram.write(0x3000, std::span<const std::uint8_t>(pe.data(), 1)) !=
        wx360::memory::MemoryResult::PermissionDenied) {
        std::cerr << "Mapped code section should not be writable\n";
        return EXIT_FAILURE;
    }
    // A denied destination page must be detected before the mapper writes
    // even the first writable page of the image.
    wx360::memory::PhysicalMemory protected_ram(0x10000);
    const std::array<std::uint8_t, 4> sentinel{{0xA1, 0xB2, 0xC3, 0xD4}};
    if (protected_ram.write(0x2000, sentinel) != wx360::memory::MemoryResult::Ok ||
        protected_ram.set_page_permissions(0x3000, wx360::memory::PhysicalMemory::page_size,
            static_cast<std::uint8_t>(wx360::memory::Access::Read)) !=
                wx360::memory::MemoryResult::Ok) {
        std::cerr << "Could not prepare protected PE destination fixture\\n";
        return EXIT_FAILURE;
    }
    const auto rejected = wx360::xex::XexPeMapper::map(
        pe, protected_ram, 0x2000, 0x82000000);
    std::array<std::uint8_t, 4> unchanged{};
    if (rejected ||
        protected_ram.read(0x2000, unchanged) != wx360::memory::MemoryResult::Ok ||
        unchanged != sentinel) {
        std::cerr << "PE mapper partially modified memory after permission preflight failure\\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
