#pragma once
#include "wx360/crypto/sha1.hpp"
#include <algorithm>
#include <array>
#include <utility>
#include <cstdint>
#include <span>
#include <vector>

namespace wx360::xex {
struct PageDescriptor {
    std::uint32_t raw{};
    std::uint32_t type{};
    std::uint32_t page_count{};
    crypto::Sha1Digest digest{};
};
struct SecurityInfo {
    std::uint32_t header_size{}, image_size{}, image_flags{}, load_address{};
    std::uint32_t import_table_count{}, export_table{}, region{}, allowed_media_types{};
    std::array<std::uint8_t,256> rsa_signature{};
    crypto::Sha1Digest section_digest{}, import_table_digest{}, header_digest{};
    std::array<std::uint8_t,16> encrypted_image_key{};
    std::vector<PageDescriptor> pages;
};
enum class SecurityParseError : std::uint8_t { Ok, Truncated, InvalidSize, TooManyPages };

[[nodiscard]] inline SecurityParseError parse_security_info(
    std::span<const std::uint8_t> file, std::uint32_t offset, SecurityInfo& out) {
    constexpr std::size_t fixed=0x184;
    if(offset>file.size() || file.size()-offset<fixed) return SecurityParseError::Truncated;
    auto be32=[&](std::size_t p){return (std::uint32_t(file[p])<<24)|(std::uint32_t(file[p+1])<<16)|(std::uint32_t(file[p+2])<<8)|file[p+3];};
    SecurityInfo s;
    s.header_size=be32(offset); s.image_size=be32(offset+4);
    if(s.header_size<fixed || s.header_size>file.size()-offset || !s.image_size)
        return SecurityParseError::InvalidSize;
    std::copy_n(file.begin()+offset+8,256,s.rsa_signature.begin());
    s.image_flags=be32(offset+0x10C); s.load_address=be32(offset+0x110);
    std::copy_n(file.begin()+offset+0x114,20,s.section_digest.begin());
    s.import_table_count=be32(offset+0x128);
    std::copy_n(file.begin()+offset+0x12C,20,s.import_table_digest.begin());
    std::copy_n(file.begin()+offset+0x150,16,s.encrypted_image_key.begin());
    s.export_table=be32(offset+0x160);
    std::copy_n(file.begin()+offset+0x164,20,s.header_digest.begin());
    s.region=be32(offset+0x178); s.allowed_media_types=be32(offset+0x17C);
    const auto count=be32(offset+0x180);
    if(count>65536u || std::uint64_t(fixed)+std::uint64_t(count)*24u>s.header_size)
        return SecurityParseError::TooManyPages;
    s.pages.reserve(count);
    for(std::uint32_t i=0;i<count;++i){
        const auto p=offset+fixed+std::size_t(i)*24u;
        PageDescriptor d; d.raw=be32(p); d.type=d.raw&0xFu; d.page_count=d.raw>>4;
        if(d.type<1 || d.type>3 || !d.page_count) return SecurityParseError::InvalidSize;
        std::copy_n(file.begin()+p+4,20,d.digest.begin()); s.pages.push_back(d);
    }
    out=std::move(s); return SecurityParseError::Ok;
}

[[nodiscard]] inline bool verify_sha1(std::span<const std::uint8_t> bytes,
                                       const crypto::Sha1Digest& expected) {
    const auto actual=crypto::sha1(bytes);
    std::uint8_t difference=0;
    for(std::size_t i=0;i<actual.size();++i) difference|=actual[i]^expected[i];
    return difference==0;
}

enum class PageHashResult : std::uint8_t {
    Ok, ImageSizeMismatch, DescriptorOverflow, DigestMismatch
};

// XEX2 uses 4 KiB pages only when IMAGE_PAGE_SIZE_4KB is set;
// otherwise page descriptors cover 64 KiB units.
inline constexpr std::uint32_t kXexImagePageSize4Kb = 0x10000000u;

[[nodiscard]] inline std::size_t xex_page_size(const SecurityInfo& security) noexcept {
    return (security.image_flags & kXexImagePageSize4Kb) != 0 ? 0x1000u : 0x10000u;
}

// XEX page hashes form a SHA-1 chain rooted at SecurityInfo.section_digest.
// Each digest covers the declared page range (zero padded at EOF) followed by
// that descriptor's exact 24-byte big-endian representation. The descriptor's
// digest links to the next descriptor; it is not a standalone page checksum.
inline constexpr std::uint32_t kXexImagePageSize4Kb = 0x10000000u;

[[nodiscard]] inline std::size_t xex_page_size(const SecurityInfo& security) noexcept {
    return (security.image_flags & kXexImagePageSize4Kb) != 0 ? 0x1000u : 0x10000u;
}

[[nodiscard]] inline PageHashResult verify_page_hashes(
    std::span<const std::uint8_t> image, const SecurityInfo& security,
    std::size_t page_size = 0) {
    if (page_size == 0) page_size = xex_page_size(security);
    if (image.size() != security.image_size || image.empty() || page_size == 0 ||
        security.pages.empty())
        return PageHashResult::ImageSizeMismatch;

    const auto required_pages = (image.size() + page_size - 1u) / page_size;
    std::size_t declared_pages = 0;
    for (const auto& page : security.pages) {
        if (page.page_count == 0 || page.type < 1 || page.type > 3 ||
            declared_pages > required_pages ||
            page.page_count > required_pages - declared_pages)
            return PageHashResult::DescriptorOverflow;
        declared_pages += page.page_count;
    }
    if (declared_pages != required_pages)
        return PageHashResult::ImageSizeMismatch;

    auto expected = security.section_digest;
    std::size_t offset = 0;
    for (const auto& page : security.pages) {
        const auto declared_size = static_cast<std::size_t>(page.page_count) * page_size;
        const auto actual_size = std::min(declared_size, image.size() - offset);
        std::vector<std::uint8_t> authenticated;
        authenticated.reserve(declared_size + 24u);
        authenticated.insert(authenticated.end(),
            image.begin() + static_cast<std::ptrdiff_t>(offset),
            image.begin() + static_cast<std::ptrdiff_t>(offset + actual_size));
        authenticated.resize(declared_size, 0);
        const auto raw = (page.page_count << 4u) | (page.type & 0xFu);
        authenticated.push_back(static_cast<std::uint8_t>(raw >> 24u));
        authenticated.push_back(static_cast<std::uint8_t>(raw >> 16u));
        authenticated.push_back(static_cast<std::uint8_t>(raw >> 8u));
        authenticated.push_back(static_cast<std::uint8_t>(raw));
        authenticated.insert(authenticated.end(), page.digest.begin(), page.digest.end());
        if (!verify_sha1(authenticated, expected))
            return PageHashResult::DigestMismatch;
        expected = page.digest;
        offset += actual_size;
    }
    return offset == image.size() ? PageHashResult::Ok
                                  : PageHashResult::ImageSizeMismatch;
}

} // namespace wx360::xex
