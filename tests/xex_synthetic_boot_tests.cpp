#include "onix360/machine/xenon_core.hpp"
#include "onix360/xex/xex_image_loader.hpp"
#include "onix360/crypto/sha1.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace {
void synthetic_kernel_service(onix360::cpu::XenonThreadState& state) noexcept {
    state.gpr[3] = 0xBEEFU;
}
void put16(std::vector<std::uint8_t>& b, std::size_t o, std::uint16_t v) {
    b[o] = static_cast<std::uint8_t>(v); b[o + 1] = static_cast<std::uint8_t>(v >> 8);
}
void put32(std::vector<std::uint8_t>& b, std::size_t o, std::uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) b[o + i] = static_cast<std::uint8_t>(v >> (8 * i));
}
void put_be32(std::vector<std::uint8_t>& b, std::size_t o, std::uint32_t v) {
    b[o] = static_cast<std::uint8_t>(v >> 24); b[o + 1] = static_cast<std::uint8_t>(v >> 16);
    b[o + 2] = static_cast<std::uint8_t>(v >> 8); b[o + 3] = static_cast<std::uint8_t>(v);
}
std::vector<std::uint8_t> make_synthetic_xbox_pe() {
    constexpr std::size_t pe = 0x80, coff = pe + 4, optional = coff + 20;
    constexpr std::size_t section = optional + 224, raw = 0x200;
    std::vector<std::uint8_t> image(0x400, 0);
    put16(image, 0, 0x5A4D); put32(image, 0x3C, static_cast<std::uint32_t>(pe));
    put32(image, pe, 0x00004550); put16(image, coff, 0x01F2);
    put16(image, coff + 2, 1); put16(image, coff + 16, 224);
    put16(image, optional, 0x010B); put32(image, optional + 16, 0x1000);
    put32(image, optional + 28, 0x82000000); put32(image, optional + 32, 0x1000);
    put32(image, optional + 36, 0x200); put32(image, optional + 56, 0x2000);
    put32(image, optional + 60, 0x200); put16(image, optional + 68, 14);
    put32(image, optional + 92, 16);
    image[section] = '.'; image[section + 1] = 't'; image[section + 2] = 'e';
    image[section + 3] = 'x'; image[section + 4] = 't';
    put32(image, section + 8, 0x100); put32(image, section + 12, 0x1000);
    put32(image, section + 16, 0x200); put32(image, section + 20, static_cast<std::uint32_t>(raw));
    put32(image, section + 36, 0x60000020);
    // Synthetic PPC payload: ADDI r3,0,42; ADDI r4,0,7; STW r3,0x20(0); LWZ r5,0x20(0).
    put_be32(image, raw, 0x3860002AU);
    put_be32(image, raw + 4U, 0x38800007U);
    put_be32(image, raw + 8U, 0x90600020U);
    put_be32(image, raw + 12U, 0x80A00020U);
    put_be32(image, raw + 16U, 0x38000042U); // li r0, 0x42
    put_be32(image, raw + 20U, 0x44000002U); // sc -> host-backed test service
    return image;
}
std::vector<std::uint8_t> make_synthetic_xex2(const std::vector<std::uint8_t>& pe) {
    constexpr std::size_t format_info = 0x80, security_info = 0x100, pe_offset = 0x300;
    std::vector<std::uint8_t> xex(pe_offset + pe.size(), 0);
    xex[0] = 'X'; xex[1] = 'E'; xex[2] = 'X'; xex[3] = '2';
    put_be32(xex, 8, pe_offset); put_be32(xex, 16, security_info); put_be32(xex, 20, 2);
    put_be32(xex, 24, 0x00010100); put_be32(xex, 28, 0x82001000);
    put_be32(xex, 32, 0x000003FF); put_be32(xex, 36, format_info);
    put_be32(xex, format_info, 8);
    put_be32(xex, security_info, 0x184 + 24);
    put_be32(xex, security_info + 4, static_cast<std::uint32_t>(pe.size()));
    put_be32(xex, security_info + 0x10C, 0x10000000); // 4 KiB page mode
    put_be32(xex, security_info + 0x180, 1);
    put_be32(xex, security_info + 0x184, 0x11); // one code page
    std::copy(pe.begin(), pe.end(), xex.begin() + pe_offset);
    std::vector<std::uint8_t> padded_page(0x1000, 0);
    std::copy(pe.begin(), pe.end(), padded_page.begin());
    const auto page_digest = onix360::crypto::sha1(padded_page);
    std::copy(page_digest.begin(), page_digest.end(), xex.begin() + security_info + 0x188);
    std::vector<std::uint8_t> authenticated = padded_page;
    authenticated.insert(authenticated.end(), xex.begin() + security_info + 0x184,
                         xex.begin() + security_info + 0x19C);
    const auto root_digest = onix360::crypto::sha1(authenticated);
    std::copy(root_digest.begin(), root_digest.end(), xex.begin() + security_info + 0x114);
    return xex;
}
}

int main() {
    using namespace onix360;
    const auto pe = make_synthetic_xbox_pe();
    const auto xex_bytes = make_synthetic_xex2(pe);
    const auto path = std::filesystem::temp_directory_path() / "onix360_synthetic_boot.xex";
    {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(xex_bytes.data()),
                  static_cast<std::streamsize>(xex_bytes.size()));
        if (!out) return EXIT_FAILURE;
    }
    const auto inspected = xex::XexImageLoader::inspect(path);
    xex::FullXexImage complete_image{};
    const auto complete_read = xex::XexImageLoader::read_complete(path, complete_image);
    std::error_code ec; std::filesystem::remove(path, ec);
    if (complete_read != xex::LoadError::None ||
        complete_image.bytes.size() != xex_bytes.size() ||
        complete_image.security.image_size != pe.size() ||
        complete_image.format.compression != xex::XexCompression::None) {
        std::cerr << "Complete synthetic XEX2 read/SecurityInfo parse failed\\n";
        return EXIT_FAILURE;
    }
    if (!inspected || inspected.plan.entry_point != 0x82001000) {
        std::cerr << "Synthetic XEX2 header inspection failed\n"; return EXIT_FAILURE;
    }
    xex::Header header{};
    if (xex::parse_header(xex_bytes, header, xex_bytes.size()) != xex::ParseResult::Ok) {
        std::cerr << "Synthetic XEX2 parse failed\n"; return EXIT_FAILURE;
    }
    xex::FileFormatInfo format{};
    if (xex::parse_file_format_info(xex_bytes, header, format) != xex::FormatParseResult::Ok ||
        format.image_size != pe.size() || format.compression != xex::XexCompression::None ||
        format.encryption != xex::XexEncryption::None) {
        std::cerr << "Synthetic XEX2 file-format metadata failed\n"; return EXIT_FAILURE;
    }
    memory::PhysicalMemory ram(0x10000);
    constexpr std::uint64_t physical_base = 0x2000;
    constexpr std::uint32_t guest_base = 0x82000000;
    const auto validated = xex::XexImageLoader::load_validated(
        complete_image, ram, physical_base, guest_base);
    if (!validated) {
        std::cerr << "Synthetic XEX2 integrity validation or PE mapping failed\n";
        return EXIT_FAILURE;
    }
    machine::XenonCore core;
    if (core.start_xex(*validated, ram) != machine::LaunchResult::Started) {
        std::cerr << "XenonCore rejected the loader-validated synthetic XEX2\n";
        return EXIT_FAILURE;
    }
    const auto first_batch = core.run(ram, 2);
    if (first_batch.instructions_executed != 2 ||
        !first_batch.instruction_limit_reached ||
        first_batch.stop_reason != machine::RunResult::Executed ||
        core.state.gpr[3] != 42 || core.state.gpr[4] != 7 ||
        core.state.pc != validated->entry_point() + 8) {
        std::cerr << "Bounded Xenon batch did not execute first two XEX instructions\\n";
        return EXIT_FAILURE;
    }
    const auto resumed_batch = core.run(ram, 2);
    if (resumed_batch.instructions_executed != 2 ||
        !resumed_batch.instruction_limit_reached ||
        resumed_batch.stop_reason != machine::RunResult::Executed ||
        core.state.gpr[5] != 42 ||
        core.state.pc != validated->entry_point() + 16) {
        std::cerr << "Resumed Xenon batch did not complete synthetic XEX payload\\n";
        return EXIT_FAILURE;
    }
    // Launch the same validated image through its guest virtual entry.
    // The PE code page is physically at 0x3000 and mapped at 0x82001000.
    kernel::KernelRuntime synthetic_kernel;
    if (!synthetic_kernel.register_service(0x42, &synthetic_kernel_service))
        return EXIT_FAILURE;
    machine::XenonCore mmu_core;
    mmu_core.attach_kernel_runtime(synthetic_kernel);
    if (mmu_core.map_validated_xex(*validated) != memory::MemoryResult::Ok ||
        mmu_core.map_guest_page(0x00000000, 0x6000, 7) != memory::MemoryResult::Ok ||
        mmu_core.start_xex(*validated, ram) != machine::LaunchResult::Started ||
        mmu_core.state.pc != validated->guest_entry_point()) {
        std::cerr << "Automatic PE-section MMU mapping or XEX entry validation failed\\n";
        return EXIT_FAILURE;
    }
    const auto mmu_batch = mmu_core.run(ram, 4);
    if (mmu_batch.instructions_executed != 4 || core.state.gpr[3] != 42 ||
        mmu_core.state.gpr[3] != 42 || mmu_core.state.gpr[4] != 7 ||
        mmu_core.state.gpr[5] != 42 || mmu_core.state.pc != 0x82001010) {
        std::cerr << "MMU-backed XEX code/data execution failed\\n";
        return EXIT_FAILURE;
    }
    const auto kernel_batch = mmu_core.run(ram, 2);
    if (kernel_batch.instructions_executed != 2 ||
        mmu_core.state.gpr[3] != 0xBEEFU ||
        mmu_core.state.pc != 0x82001018) {
        std::cerr << "XEX guest system-call dispatch failed\\n";
        return EXIT_FAILURE;
    }
    // Verify guest data translation independently across the same address space.
    const std::array<std::uint8_t, 4> guest_data{0x12, 0x34, 0x56, 0x78};
    std::array<std::uint8_t, 4> guest_readback{};
    if (mmu_core.write_guest(ram, 0x24, guest_data) != memory::MemoryResult::Ok ||
        mmu_core.read_guest(ram, 0x24, guest_readback) != memory::MemoryResult::Ok ||
        guest_readback != guest_data) {
        std::cerr << "Guest virtual data read/write translation failed\\n";
        return EXIT_FAILURE;
    }

    // The command-buffer bridge must fetch guest-visible PM4 words through MMU.
    if (!mmu_core.gpu.configure_framebuffer(16, 16)) return EXIT_FAILURE;
    const std::uint32_t pm4_words[4] = {
        0xC000F100U, 0xFF336699U, 0xC000F200U, 0x00000000U
    };
    std::array<std::uint8_t, 16> pm4_bytes{};
    for (unsigned w = 0; w < 4; ++w)
        for (unsigned b = 0; b < 4; ++b)
            pm4_bytes[w * 4U + b] = static_cast<std::uint8_t>(pm4_words[w] >> (24U - b * 8U));
    if (ram.write(0x7000, pm4_bytes) != memory::MemoryResult::Ok ||
        mmu_core.map_guest_page(0x82004000, 0x7000, 3) != memory::MemoryResult::Ok ||
        !mmu_core.execute_gpu_command_buffer(ram, 0x82004000, 4) ||
        mmu_core.gpu.framebuffer().present_count() != 1 ||
        mmu_core.gpu.framebuffer().pixels().front() != 0xFF336699U) {
        std::cerr << "MMU-backed guest PM4 clear/present bridge failed\\n";
        return EXIT_FAILURE;
    }
    std::cout << "Onix360 synthetic XEX2-to-bounded-Xenon-batches test passed\n";
    return EXIT_SUCCESS;
}
