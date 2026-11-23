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
    be32(security,0x110,0x82000000);be32(security,0x128,0);
    be32(security,0x180,1);be32(security,0x184,0x21);
    wx360::xex::SecurityInfo parsed{};
    if(wx360::xex::parse_security_info(security,0,parsed)!=wx360::xex::SecurityParseError::Ok ||
       parsed.image_size!=0x2000 || parsed.pages.size()!=1 ||
       parsed.pages[0].type!=1 || parsed.pages[0].page_count!=2){
        std::cerr<<"XEX security info parse failed\n";return EXIT_FAILURE;
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
