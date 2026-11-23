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

// Page descriptors cover the image in 4 KiB units. Hashes are checked
// independently and the input must match the declared image size exactly.
[[nodiscard]] inline PageHashResult verify_page_hashes(
    std::span<const std::uint8_t> image, const SecurityInfo& security,
    std::size_t page_size = 0x1000) {
    if (page_size == 0 || image.size() != security.image_size)
        return PageHashResult::ImageSizeMismatch;
    std::size_t offset = 0;
    for (const auto& page : security.pages) {
        if (page.page_count > (image.size() - offset) / page_size)
            return PageHashResult::DescriptorOverflow;
        const auto bytes = static_cast<std::size_t>(page.page_count) * page_size;
        if (!verify_sha1(image.subspan(offset, bytes), page.digest))
            return PageHashResult::DigestMismatch;
        offset += bytes;
    }
    return offset == image.size() ? PageHashResult::Ok
                                  : PageHashResult::ImageSizeMismatch;
}
} // namespace wx360::xex
