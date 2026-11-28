#pragma once
#include <cstdint>
#include <optional>

namespace wx360::memory {

enum class XenonAddressRegion : std::uint8_t {
    Virtual4K,
    Virtual64K,
    XexImage64K,
    XexEncryptedImage64K,
    XexImage4K,
    Physical64K,
    Physical16M,
    Physical4K,
    Unmapped
};

struct XenonAddressRegionInfo {
    XenonAddressRegion region{XenonAddressRegion::Unmapped};
    std::uint32_t page_size{};
    bool guest_virtual{};
    bool physical_alias{};
    bool encrypted_xex_window{};
};

// Software-visible Xbox 360 guest address-space windows, as modelled by Xenia.
// These ranges describe emulator mapping policy, not the hidden Xenon
// hypervisor/L2 physical-address translation table.
[[nodiscard]] constexpr std::optional<std::uint32_t>
xenon_physical_alias_address(std::uint32_t address) noexcept {
    if (address < 0xA0000000U) return std::nullopt;
    return address & 0x1FFFFFFFU;
}

[[nodiscard]] constexpr XenonAddressRegionInfo classify_xenon_address(
    std::uint32_t address) noexcept {
    if (address < 0x40000000U)
        return {XenonAddressRegion::Virtual4K, 0x1000U, true, false, false};
    if (address < 0x80000000U)
        return {XenonAddressRegion::Virtual64K, 0x10000U, true, false, false};
    if (address < 0x8C000000U)
        return {XenonAddressRegion::XexImage64K, 0x10000U, true, false, false};
    if (address < 0x90000000U)
        return {XenonAddressRegion::XexEncryptedImage64K, 0x10000U, true, false, true};
    if (address < 0xA0000000U)
        return {XenonAddressRegion::XexImage4K, 0x1000U, true, false, false};
    if (address < 0xC0000000U)
        return {XenonAddressRegion::Physical64K, 0x10000U, false, true, false};
    if (address < 0xE0000000U)
        return {XenonAddressRegion::Physical16M, 0x1000000U, false, true, false};
    return {XenonAddressRegion::Physical4K, 0x1000U, false, true, false};
}

} // namespace wx360::memory
