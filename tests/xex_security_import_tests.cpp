#include "wx360/crypto/sha1.hpp"
#include "wx360/xex/xex_security_info.hpp"
#include "wx360/xex/xex_import_table.hpp"
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

static void be16(std::vector<std::uint8_t>& b,std::size_t p,std::uint16_t v){b[p]=v>>8;b[p+1]=v;}
static void be32(std::vector<std::uint8_t>& b,std::size_t p,std::uint32_t v){b[p]=v>>24;b[p+1]=v>>16;b[p+2]=v>>8;b[p+3]=v;}

int main(){
    constexpr std::string_view abc="abc";
    const auto digest=wx360::crypto::sha1(std::span(
        reinterpret_cast<const std::uint8_t*>(abc.data()),abc.size()));
    constexpr std::array<std::uint8_t,20> expected{
        0xA9,0x99,0x3E,0x36,0x47,0x06,0x81,0x6A,0xBA,0x3E,
        0x25,0x71,0x78,0x50,0xC2,0x6C,0x9C,0xD0,0xD8,0x9D};
    if(digest!=expected){std::cerr<<"SHA-1 known-answer test failed\n";return EXIT_FAILURE;}

    std::vector<std::uint8_t> security(0x184+24,0);
    be32(security,0,0x184+24);be32(security,4,0x2000);
    be32(security,0x10C,0x10000000); // XEX_IMAGE_PAGE_SIZE_4KB
    be32(security,0x110,0x82000000);be32(security,0x128,0);
    be32(security,0x180,1);be32(security,0x184,0x21);
    wx360::xex::SecurityInfo parsed{};
    if(wx360::xex::parse_security_info(security,0,parsed)!=wx360::xex::SecurityParseError::Ok ||
       parsed.image_size!=0x2000 || parsed.pages.size()!=1 ||
       parsed.pages[0].type!=1 || parsed.pages[0].page_count!=2){
        std::cerr<<"XEX security info parse failed\n";return EXIT_FAILURE;
    }

    std::vector<std::uint8_t> page_image(0x2000, 0x5A);
    parsed.pages[0].raw = 0x21;
    parsed.pages[0].digest = wx360::crypto::sha1(page_image);
    std::vector<std::uint8_t> authenticated(0x2000 + 24, 0);
    std::copy(page_image.begin(), page_image.end(), authenticated.begin());
    authenticated[0x2000] = 0x00;
    authenticated[0x2001] = 0x00;
    authenticated[0x2002] = 0x00;
    authenticated[0x2003] = 0x21;
    std::copy(parsed.pages[0].digest.begin(), parsed.pages[0].digest.end(),
              authenticated.begin() + 0x2004);
    parsed.section_digest = wx360::crypto::sha1(authenticated);
    if (wx360::xex::verify_page_hashes(page_image, parsed) !=
        wx360::xex::PageHashResult::Ok) {
        std::cerr << "XEX chained page SHA-1 positive vector failed\\n"; return EXIT_FAILURE;
    }
    page_image[0] ^= 1;
    if (wx360::xex::verify_page_hashes(page_image, parsed) !=
        wx360::xex::PageHashResult::DigestMismatch) {
        std::cerr << "Corrupted XEX page was not rejected\\n"; return EXIT_FAILURE;
    }
    page_image[0] ^= 1;
    parsed.section_digest[0] ^= 1;
    if (wx360::xex::verify_page_hashes(page_image, parsed) !=
        wx360::xex::PageHashResult::DigestMismatch) {
        std::cerr << "Corrupted XEX root digest was not rejected\\n"; return EXIT_FAILURE;
    }
    parsed.section_digest[0] ^= 1;
    parsed.pages[0].raw = 0x31;
    if (wx360::xex::verify_page_hashes(page_image, parsed) !=
        wx360::xex::PageHashResult::DigestMismatch) {
        std::cerr << "Corrupted XEX page descriptor was not rejected\\n"; return EXIT_FAILURE;
    }

    // Without the 4 KiB flag, XEX2 page hashes use 64 KiB units.
    wx360::xex::SecurityInfo large_pages{};
    large_pages.image_size = 0x10000;
    std::vector<std::uint8_t> large_image(0x10000, 0xA5);
    wx360::xex::PageDescriptor large_descriptor{};
    large_descriptor.raw = 0x11;
    large_descriptor.type = 1;
    large_descriptor.page_count = 1;
    large_descriptor.digest = wx360::crypto::sha1(large_image);
    large_pages.pages.push_back(large_descriptor);
    if (wx360::xex::xex_page_size(large_pages) != 0x10000 ||
        wx360::xex::verify_page_hashes(large_image, large_pages) !=
            wx360::xex::PageHashResult::Ok) {
        std::cerr << "XEX 64 KiB page SHA-1 validation failed\\n";
        return EXIT_FAILURE;
    }

    // XEX import library directory: one padded name and one library record.
    std::vector<std::uint8_t> imports(12+12+0x2C,0);
    be32(imports,0,imports.size());be32(imports,4,12);be32(imports,8,1);
    imports[12]='x';imports[13]='b';imports[14]='o';imports[15]='x';
    imports[16]='k';imports[17]='r';imports[18]='n';imports[19]='l';
    be32(imports,24,0x2C);be32(imports,24+0x18,0x12345678);
    be32(imports,24+0x1C,0x00010002);be32(imports,24+0x20,0x00010000);
    be16(imports,24+0x24,0);be16(imports,24+0x26,1);
    be32(imports,24+0x28,0x82001000);
    wx360::xex::ImportTable table{};
    if(wx360::xex::parse_import_table(imports,0,table)!=wx360::xex::ImportTableError::Ok ||
       table.libraries.size()!=1 || table.libraries[0].name!="xboxkrnl" ||
       table.libraries[0].import_thunks.size()!=1 ||
       table.libraries[0].import_thunks[0]!=0x82001000){
        std::cerr<<"XEX import library parser failed\n";return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
