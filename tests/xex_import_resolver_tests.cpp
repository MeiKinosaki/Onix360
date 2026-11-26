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
        XexImport{"XBOXKRNL.EXE", 0x123, 0x220, ImportKind::Function, 0x200},
        XexImport{"xboxkrnl.exe", 0x124, 0, ImportKind::Variable, 0x204}
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
    const std::array<std::uint32_t,4> expected_thunk{
        0x44000042U, 0x4E800020U, 0x60000000U, 0x60000000U
    };
    bool thunk_matches = true;
    for (std::size_t i = 0; i < expected_thunk.size(); ++i)
        thunk_matches = thunk_matches && is_be_address(0x220 + i * 4, expected_thunk[i]);
    if (!is_be_address(0x200, 0xDEADC0DE) || !is_be_address(0x204, 0x82002000) ||
        !thunk_matches) {
        std::cerr << "Import slots or executable Xenon thunk code were not emitted correctly\\n";
        return EXIT_FAILURE;
    }

    const std::array wrong_kind{
        XexImport{"xboxkrnl.exe", 0x124, 0x228, ImportKind::Function, 0x208}
    };
    if (resolver.resolve(wrong_kind, ram) != ImportResolveError::KindMismatch ||
        bytes[0x208] != 0) {
        std::cerr << "Function/variable kind mismatch was not rejected atomically\n";
        return EXIT_FAILURE;
    }

    const std::array missing{
        XexImport{"xam.xex", 0x999, 0x22C, ImportKind::Function, 0x20C}
    };
    const std::array batch_with_late_failure{
        XexImport{"xboxkrnl.exe", 0x123, 0x240, ImportKind::Function, 0x214},
        XexImport{"xam.xex", 0x999, 0x24C, ImportKind::Function, 0x218}
    };
    if (resolver.resolve(batch_with_late_failure, ram) != ImportResolveError::MissingExport ||
        bytes[0x214] != 0 || bytes[0x218] != 0) {
        std::cerr << "Late missing export caused partial import patching\\n";
        return EXIT_FAILURE;
    }
    if (resolver.resolve(missing, ram) != ImportResolveError::MissingExport ||
        bytes[0x20C] != 0) {
        std::cerr << "Unknown import was not rejected without patching\n";
        return EXIT_FAILURE;
    }

    const std::array bad_range{
        XexImport{"xboxkrnl.exe", 0x123, 0x3FFF, ImportKind::Function, 0x210}
    };
    if (resolver.resolve(bad_range, ram) != ImportResolveError::InvalidThunk) {
        std::cerr << "Out-of-range thunk was not rejected\n";
        return EXIT_FAILURE;
    }

    const std::array duplicate_thunk{
        XexImport{"xboxkrnl.exe", 0x123, 0x230, ImportKind::Function, 0x300},
        XexImport{"xboxkrnl.exe", 0x124, 0, ImportKind::Variable, 0x300}
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
    if (invalid_registry.register_export({"", 2, 0x82004000, ImportKind::Function}) ||
        invalid_registry.register_export({"xboxkrnl.exe", 3, 0x82004000,
            static_cast<ImportKind>(0xFF)})) {
        std::cerr << "Malformed export metadata was accepted\n";
        return EXIT_FAILURE;
    }
    const std::array malformed_imports{
        XexImport{"", 0x123, 0x304, ImportKind::Function}
    };
    if (resolver.resolve(malformed_imports, ram) != ImportResolveError::MissingExport ||
        bytes[0x304] != 0) {
        std::cerr << "Empty import library was not rejected atomically\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
