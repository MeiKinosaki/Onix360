#include "wx360/machine/xenon_core.hpp"
#include "wx360/xex/xex_pe_mapper.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
void put16(std::vector<std::uint8_t>& b, std::size_t o, std::uint16_t v) {
    b[o] = static_cast<std::uint8_t>(v);
    b[o + 1] = static_cast<std::uint8_t>(v >> 8);
}
void put32(std::vector<std::uint8_t>& b, std::size_t o, std::uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) b[o + i] = static_cast<std::uint8_t>(v >> (8 * i));
}
std::vector<std::uint8_t> make_synthetic_xbox_pe() {
    constexpr std::size_t pe = 0x80, coff = pe + 4, optional = coff + 20;
    constexpr std::size_t section = optional + 224, raw = 0x200;
    std::vector<std::uint8_t> image(0x400, 0);
    put16(image, 0, 0x5A4D);                 // MZ
    put32(image, 0x3C, static_cast<std::uint32_t>(pe));
    put32(image, pe, 0x00004550);            // PE\0\0
    put16(image, coff, 0x01F2);              // IMAGE_FILE_MACHINE_POWERPCBE
    put16(image, coff + 2, 1);               // one section
    put16(image, coff + 16, 224);            // PE32 optional header
    put16(image, optional, 0x010B);          // PE32
    put32(image, optional + 16, 0x1000);     // entry RVA
    put32(image, optional + 28, 0x82000000); // preferred image base
    put32(image, optional + 32, 0x1000);     // section alignment
    put32(image, optional + 36, 0x200);      // file alignment
    put32(image, optional + 56, 0x2000);     // image size
    put32(image, optional + 60, 0x200);      // headers size
    put16(image, optional + 68, 14);         // Xbox subsystem
    put32(image, optional + 92, 16);         // data directory count
    image[section] = '.';
    image[section + 1] = 't';
    image[section + 2] = 'e';
    image[section + 3] = 'x';
    image[section + 4] = 't';
    put32(image, section + 8, 0x100);
    put32(image, section + 12, 0x1000);
    put32(image, section + 16, 0x200);
    put32(image, section + 20, static_cast<std::uint32_t>(raw));
    put32(image, section + 36, 0x60000020); // code, execute, read
    // addi r3,r0,42 — first instruction at the PE entry point.
    image[raw] = 0x38; image[raw + 1] = 0x60;
    image[raw + 2] = 0x00; image[raw + 3] = 0x2A;
    return image;
}
}

int main() {
    using namespace wx360;
    memory::PhysicalMemory ram(0x10000);
    const auto pe = make_synthetic_xbox_pe();
    constexpr std::uint64_t physical_base = 0x2000;
    constexpr std::uint32_t guest_base = 0x82000000;

    const auto mapped = xex::XexPeMapper::map(
        pe, ram, physical_base, guest_base, 0);
    if (!mapped) {
        std::cerr << "Synthetic PE mapping failed: "
                  << static_cast<unsigned>(mapped.error) << '\n';
        return EXIT_FAILURE;
    }

    xex::LaunchPlan plan{};
    plan.entry_point = 0x1000;
    plan.decryption_complete = true;   // synthetic plaintext fixture
    plan.decompression_complete = true;
    plan.pe_mapped = true;
    plan.kernel_imports_resolved = true; // fixture has no imports
    plan.mapped_entry_point = mapped.mapped_entry;

    machine::XenonCore core;
    if (core.start_xex(plan, ram) != machine::LaunchResult::Started) {
        std::cerr << "XenonCore rejected the mapped synthetic image\n";
        return EXIT_FAILURE;
    }
    if (core.step(ram) != machine::RunResult::Executed ||
        core.state.gpr[3] != 42 || core.state.pc != mapped.mapped_entry + 4) {
        std::cerr << "Synthetic XEX entry instruction did not execute\n";
        return EXIT_FAILURE;
    }
    std::cout << "WX360 synthetic PE-to-Xenon boot test passed\n";
    return EXIT_SUCCESS;
}
