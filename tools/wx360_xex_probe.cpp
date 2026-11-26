#include "wx360/kernel/kernel_runtime.hpp"
#include "wx360/machine/xenon_core.hpp"
#include "wx360/memory/physical_memory.hpp"
#include "wx360/xex/xex_image_loader.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>

namespace {
constexpr std::uint64_t kPhysicalBase = 0x1000;
constexpr std::size_t kInstructionBudget = 100000;

std::uint32_t read_le32(const std::vector<std::uint8_t>& bytes, std::size_t at) {
    if (at > bytes.size() || bytes.size() - at < 4) return 0;
    return static_cast<std::uint32_t>(bytes[at]) |
        (static_cast<std::uint32_t>(bytes[at + 1]) << 8U) |
        (static_cast<std::uint32_t>(bytes[at + 2]) << 16U) |
        (static_cast<std::uint32_t>(bytes[at + 3]) << 24U);
}
}

int main(int argc, char** argv) {
    using namespace wx360;
    if (argc != 2) {
        std::cerr << "Usage: WX360-XEX-Probe <homebrew.xex>\n";
        return 2;
    }

    const std::filesystem::path path = std::filesystem::u8path(argv[1]);
    xex::FullXexImage image{};
    const auto read_result = xex::XexImageLoader::read_complete(path, image);
    if (read_result != xex::LoadError::None) {
        std::cerr << "XEX read/metadata parse failed (code "
                  << static_cast<unsigned>(read_result) << ").\n";
        return 3;
    }

    if (image.header.has_import_libraries) {
        std::cout << "Import libraries parsed: " << image.imports.libraries.size() << "\\n";
        for (const auto& library : image.imports.libraries)
            std::cout << "  " << library.name << ": " << library.import_count
                      << " import records\\n";
        std::cerr << "Import metadata parsed, but no version-matched real kernel/XAM "
                     "export registry is attached; launch refused safely.\\n";
        return 4;
    }
    if (image.format.compression == xex::XexCompression::Basic) {
        std::cerr << "XEX Basic compression is not yet connected to load_validated.\n";
        return 5;
    }

    std::uint32_t guest_base = image.security.load_address;
    if (guest_base == 0) {
        const auto pe = static_cast<std::size_t>(image.header.pe_data_offset);
        if (pe > image.bytes.size() || image.bytes.size() - pe < 0x40) {
            std::cerr << "Invalid PE image offset.\n";
            return 6;
        }
        const auto pe_offset = read_le32(image.bytes, pe + 0x3C);
        const auto optional = pe + pe_offset + 4U + 20U;
        if (optional > image.bytes.size() || image.bytes.size() - optional < 32) {
            std::cerr << "Truncated PE optional header.\n";
            return 6;
        }
        guest_base = read_le32(image.bytes, optional + 28U);
    }
    if (guest_base == 0) {
        std::cerr << "Could not determine XEX guest image base.\n";
        return 6;
    }

    const std::uint64_t required = kPhysicalBase + image.format.image_size + 0x10000U;
    if (required > std::numeric_limits<std::size_t>::max()) return 7;
    const auto ram_size = static_cast<std::size_t>((required + 0xFFFU) & ~0xFFFULL);
    memory::PhysicalMemory ram(ram_size);
    const auto validated = xex::XexImageLoader::load_validated(
        image, ram, kPhysicalBase, guest_base);
    if (!validated) {
        std::cerr << "XEX validation/mapping failed. RSA signature verification and "
                     "real import resolution are not implemented by this loader.\n";
        return 8;
    }

    kernel::KernelRuntime kernel;
    machine::XenonCore core;
    core.attach_kernel_runtime(kernel);
    const auto map_result = core.map_validated_xex(*validated);
    if (map_result != memory::MemoryResult::Ok) {
        std::cerr << "Could not install validated XEX guest page mappings (code "
                  << static_cast<unsigned>(map_result) << ").\\n";
        return 9;
    }
    if (core.start_xex(*validated, ram) != machine::LaunchResult::Started) {
        std::cerr << "Entry point is not executable; launch refused.\n";
        return 9;
    }

    std::cout << "WX360 XEX execution probe\n"
              << "File: " << path.filename().string() << "\n"
              << "Entry: 0x" << std::hex << core.state.pc << std::dec << "\n"
              << "RAM: " << ram.size() / (1024U * 1024U) << " MiB\n"
              << "RSA signature: NOT VERIFIED\n"
              << "Kernel exports: no real xboxkrnl/xam catalog attached\n"
              << "GPU: software PM4 scaffold only; native Xenos shaders unavailable\n"
              << "Executing at most " << kInstructionBudget << " Xenon instructions...\n";

    const auto batch = core.run(ram, kInstructionBudget);
    std::cout << "Instructions executed: " << batch.instructions_executed << "\n"
              << "Stop reason: " << static_cast<unsigned>(batch.stop_reason) << "\n"
              << "Instruction budget reached: " << (batch.instruction_limit_reached ? "yes" : "no") << "\n"
              << "Final PC: 0x" << std::hex << core.state.pc << std::dec << "\n"
              << "r3: 0x" << std::hex << core.state.gpr[3] << std::dec << "\n"
              << "GPU framebuffer presents: " << core.gpu.framebuffer().present_count() << "\n";
    return batch.instructions_executed != 0 ? 0 : 10;
}
