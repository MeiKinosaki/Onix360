#include "wx360/kernel/xex_import_resolver.hpp"

#include <array>
#include <cstdlib>
#include <iostream>

using wx360::kernel::ImportKind;
using wx360::kernel::ImportResolveError;
using wx360::kernel::KernelExport;
using wx360::kernel::XexImport;
using wx360::kernel::XexImportResolver;

int main() {
    wx360::memory::PhysicalMemory ram(0x4000);
    XexImportResolver resolver;

    // These are test-only guest addresses. Function entries represent
    // registered Xenon trampoline stubs; variables represent guest storage.
    if (!resolver.register_export({"xboxkrnl.exe", 0x123, 0x82001000, ImportKind::Function}) ||
        !resolver.register_export({"xboxkrnl.exe", 0x124, 0x82002000, ImportKind::Variable}) ||
        resolver.register_export({"XBOXKRNL.EXE", 0x123, 0x82003000, ImportKind::Function})) {
        std::cerr << "Duplicate export or invalid export registration handling failed\n";
        return EXIT_FAILURE;
    }

    const std::array imports{
        XexImport{"XBOXKRNL.EXE", 0x123, 0x200, ImportKind::Function},
        XexImport{"xboxkrnl.exe", 0x124, 0x204, ImportKind::Variable}
    };
    if (resolver.resolve(imports, ram) != ImportResolveError::None) {
        std::cerr << "Function/variable imports did not resolve\n";
        return EXIT_FAILURE;
    }
    const auto bytes = ram.raw_bytes();
    const auto is_be_address = [&](std::size_t p, std::uint32_t v) {
        return bytes[p] == static_cast<std::uint8_t>(v >> 24) &&
               bytes[p+1] == static_cast<std::uint8_t>(v >> 16) &&
               bytes[p+2] == static_cast<std::uint8_t>(v >> 8) &&
               bytes[p+3] == static_cast<std::uint8_t>(v);
    };
    if (!is_be_address(0x200, 0x82001000) || !is_be_address(0x204, 0x82002000)) {
        std::cerr << "Resolved guest addresses were not written in big-endian order\n";
        return EXIT_FAILURE;
    }

    const std::array wrong_kind{
        XexImport{"xboxkrnl.exe", 0x124, 0x208, ImportKind::Function}
    };
    if (resolver.resolve(wrong_kind, ram) != ImportResolveError::KindMismatch ||
        bytes[0x208] != 0) {
        std::cerr << "Function/variable kind mismatch was not rejected atomically\n";
        return EXIT_FAILURE;
    }

    const std::array missing{
        XexImport{"xam.xex", 0x999, 0x20C, ImportKind::Function}
    };
    if (resolver.resolve(missing, ram) != ImportResolveError::MissingExport ||
        bytes[0x20C] != 0) {
        std::cerr << "Unknown import was not rejected without patching\n";
        return EXIT_FAILURE;
    }

    const std::array bad_range{
        XexImport{"xboxkrnl.exe", 0x123, 0x3FFF, ImportKind::Function}
    };
    if (resolver.resolve(bad_range, ram) != ImportResolveError::InvalidThunk) {
        std::cerr << "Out-of-range thunk was not rejected\n";
        return EXIT_FAILURE;
    }

    const std::array duplicate_thunk{
        XexImport{"xboxkrnl.exe", 0x123, 0x300, ImportKind::Function},
        XexImport{"xboxkrnl.exe", 0x124, 0x300, ImportKind::Variable}
    };
    if (resolver.resolve(duplicate_thunk, ram) != ImportResolveError::DuplicateThunk ||
        bytes[0x300] != 0) {
        std::cerr << "Duplicate thunk patch was not rejected atomically\n";
        return EXIT_FAILURE;
    }

    XexImportResolver invalid_registry;
    if (invalid_registry.register_export({"xboxkrnl.exe", 1, 0, ImportKind::Function})) {
        std::cerr << "Zero guest address was accepted as an export\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
