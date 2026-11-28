#include "wx360/memory/xenon_address_layout.hpp"
#include <cstdlib>
#include <iostream>

int main() {
    using namespace wx360::memory;
    int failures = 0;
    auto check = [&](bool ok, const char* label) {
        if (!ok) { std::cerr << "FAIL: " << label << '\n'; ++failures; }
    };
    check(classify_xenon_address(0x00000000U).region == XenonAddressRegion::Virtual4K,
          "classifies low 4 KiB virtual window");
    check(classify_xenon_address(0x40000000U).page_size == 0x10000U,
          "classifies 64 KiB virtual window");
    check(classify_xenon_address(0x80000000U).region == XenonAddressRegion::XexImage64K,
          "classifies XEX 64 KiB image window");
    const auto encrypted = classify_xenon_address(0x8C000000U);
    check(encrypted.region == XenonAddressRegion::XexEncryptedImage64K &&
          encrypted.encrypted_xex_window,
          "classifies encrypted XEX image window");
    check(classify_xenon_address(0x90000000U).page_size == 0x1000U,
          "classifies XEX 4 KiB image window");
    check(classify_xenon_address(0xA0000000U).physical_alias,
          "classifies physical 64 KiB alias window");
    check(classify_xenon_address(0xC0000000U).page_size == 0x1000000U,
          "classifies physical 16 MiB alias window");
    check(classify_xenon_address(0xE0000000U).region == XenonAddressRegion::Physical4K &&
          classify_xenon_address(0xFFFFFFFFU).physical_alias,
          "classifies physical 4 KiB alias window through address-space end");
    check(xenon_physical_alias_address(0xA0001234U) == 0x1234U &&
          xenon_physical_alias_address(0xC0001234U) == 0x1234U &&
          xenon_physical_alias_address(0xE0001234U) == 0x1234U,
          "translates each physical alias window to the same backing offset");
    check(!xenon_physical_alias_address(0x90001234U),
          "does not classify XEX virtual image windows as physical aliases");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
