#include "wx360/kernel/xex_import_resolver.hpp"

#include <array>
#include <cstdlib>
#include <iostream>

int main() {
    wx360::memory::PhysicalMemory ram(0x4000);
    wx360::kernel::XexImportResolver resolver;
    if (!resolver.register_export({"xboxkrnl.exe", 0x123, 0x82001000}) ||
        resolver.register_export({"XBOXKRNL.EXE", 0x123, 0x82002000})) {
        std::cerr << "Case-insensitive duplicate export was not rejected\n";
        return EXIT_FAILURE;
    }
    const std::array imports{
        wx360::kernel::XexImport{"XBOXKRNL.EXE", 0x123, 0x200}
    };
    if (resolver.resolve(imports, ram) != wx360::kernel::ImportResolveError::None) {
        std::cerr << "Known ordinal import did not resolve\n";
        return EXIT_FAILURE;
    }
    const auto bytes = ram.raw_bytes();
    if (bytes[0x200] != 0x82 || bytes[0x201] != 0x00 ||
        bytes[0x202] != 0x10 || bytes[0x203] != 0x00) {
        std::cerr << "Resolved import thunk is not big-endian\n";
        return EXIT_FAILURE;
    }
    const std::array missing{
        wx360::kernel::XexImport{"xam.xex", 0x999, 0x204}
    };
    if (resolver.resolve(missing, ram) != wx360::kernel::ImportResolveError::MissingExport) {
        std::cerr << "Unknown kernel import was not rejected\n";
        return EXIT_FAILURE;
    }
    const std::array bad_range{
        wx360::kernel::XexImport{"xboxkrnl.exe", 0x123, 0x3FFF}
    };
    if (resolver.resolve(bad_range, ram) != wx360::kernel::ImportResolveError::InvalidThunk) {
        std::cerr << "Out-of-range thunk was not rejected\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
