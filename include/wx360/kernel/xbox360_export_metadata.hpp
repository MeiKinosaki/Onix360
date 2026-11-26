#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace wx360::kernel {

// Names, ordinals and kinds transcribed from Xenia's public export tables.
// This is metadata only: an entry is NOT a callable implementation until a
// matching host service and guest trampoline are registered in KernelRuntime.
enum class ExportMetadataKind : std::uint8_t { Function, Variable };

struct ExportMetadata {
    std::string_view library;
    std::uint16_t ordinal;
    std::string_view name;
    ExportMetadataKind kind;
};

inline constexpr std::array<ExportMetadata, 16> kKnownExportMetadata{{
    {"xboxkrnl.exe", 0x0001, "DbgBreakPoint", ExportMetadataKind::Function},
    {"xboxkrnl.exe", 0x0002, "DbgBreakPointWithStatus", ExportMetadataKind::Function},
    {"xboxkrnl.exe", 0x0003, "DbgPrint", ExportMetadataKind::Function},
    {"xboxkrnl.exe", 0x0004, "DbgPrompt", ExportMetadataKind::Function},
    {"xboxkrnl.exe", 0x0009, "ExAllocatePool", ExportMetadataKind::Function},
    {"xboxkrnl.exe", 0x000A, "ExAllocatePoolWithTag", ExportMetadataKind::Function},
    {"xboxkrnl.exe", 0x000C, "ExConsoleGameRegion", ExportMetadataKind::Variable},
    {"xboxkrnl.exe", 0x000D, "ExCreateThread", ExportMetadataKind::Function},
    {"xboxkrnl.exe", 0x000F, "ExFreePool", ExportMetadataKind::Function},
    {"xboxkrnl.exe", 0x0010, "ExGetXConfigSetting", ExportMetadataKind::Function},
    {"xam.xex", 0x0001, "NetDll_WSAStartup", ExportMetadataKind::Function},
    {"xam.xex", 0x0002, "NetDll_WSACleanup", ExportMetadataKind::Function},
    {"xam.xex", 0x0003, "NetDll_socket", ExportMetadataKind::Function},
    {"xam.xex", 0x0004, "NetDll_closesocket", ExportMetadataKind::Function},
    {"xam.xex", 0x0005, "NetDll_shutdown", ExportMetadataKind::Function},
    {"xam.xex", 0x0006, "NetDll_ioctlsocket", ExportMetadataKind::Function},
}};

[[nodiscard]] constexpr const ExportMetadata* find_export_metadata(
    std::string_view library, std::uint16_t ordinal) noexcept {
    for (const auto& item : kKnownExportMetadata) {
        if (item.library == library && item.ordinal == ordinal) return &item;
    }
    return nullptr;
}

} // namespace wx360::kernel
