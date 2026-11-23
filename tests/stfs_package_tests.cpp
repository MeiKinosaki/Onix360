#include "wx360/storage/stfs_package.hpp"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {
void put_be32(std::vector<std::uint8_t>& b, std::size_t o, std::uint32_t v) {
    b[o] = static_cast<std::uint8_t>(v >> 24);
    b[o+1] = static_cast<std::uint8_t>(v >> 16);
    b[o+2] = static_cast<std::uint8_t>(v >> 8);
    b[o+3] = static_cast<std::uint8_t>(v);
}
void put_be64(std::vector<std::uint8_t>& b, std::size_t o, std::uint64_t v) {
    put_be32(b, o, static_cast<std::uint32_t>(v >> 32));
    put_be32(b, o+4, static_cast<std::uint32_t>(v));
}
void put_le16(std::vector<std::uint8_t>& b, std::size_t o, std::uint16_t v) {
    b[o] = static_cast<std::uint8_t>(v);
    b[o+1] = static_cast<std::uint8_t>(v >> 8);
}
void put_le24(std::vector<std::uint8_t>& b, std::size_t o, std::uint32_t v) {
    b[o] = static_cast<std::uint8_t>(v);
    b[o+1] = static_cast<std::uint8_t>(v >> 8);
    b[o+2] = static_cast<std::uint8_t>(v >> 16);
}
void put_utf16be(std::vector<std::uint8_t>& b, std::size_t o, const char* s) {
    for (std::size_t i=0;s[i] && i<20;++i) {
        b[o+i*2]=0;
        b[o+i*2+1]=static_cast<std::uint8_t>(s[i]);
    }
}
}

int main() {
    int failures=0;
    const auto path=std::filesystem::temp_directory_path()/"wx360_stfs_header_test.con";
    std::vector<std::uint8_t> bytes(wx360::storage::StfsPackageParser::kMetadataHeaderSize,0);
    bytes[0]='C';bytes[1]='O';bytes[2]='N';bytes[3]=' ';
    put_be32(bytes,0x340,static_cast<std::uint32_t>(bytes.size()));
    put_be32(bytes,0x344,0x00007000);
    put_be32(bytes,0x348,2);
    put_be64(bytes,0x34C,0x12345678);
    put_be32(bytes,0x360,0x4D5307E6);
    bytes[0x379]=0x24;
    put_le16(bytes,0x379+3,1);
    put_le24(bytes,0x379+5,0);
    put_be32(bytes,0x379+0x1C,10);
    put_be32(bytes,0x379+0x20,2);
    put_be32(bytes,0x39D,1);
    put_be64(bytes,0x3A1,0x12345678);
    put_utf16be(bytes,0x1611,"Publisher");
    put_utf16be(bytes,0x1691,"Test Game");
    {
        std::ofstream out(path,std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    }
    auto parsed=wx360::storage::StfsPackageParser::parse(path);
    if(!parsed || parsed.header.package_type!=wx360::storage::StfsPackageType::ConsoleSigned ||
       parsed.header.content_type!=0x7000 || parsed.header.title_id!=0x4D5307E6 ||
       parsed.header.file_table_block_count!=1 || parsed.header.allocated_block_count!=10 ||
       parsed.header.title_name!="Test Game" || parsed.header.publisher_name!="Publisher") {
        std::cerr<<"Valid synthetic STFS metadata was not parsed\n";++failures;
    }

    bytes[0]='X';
    {
        std::ofstream out(path,std::ios::binary|std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    }
    if(wx360::storage::StfsPackageParser::parse(path).error!=wx360::storage::StfsParseError::InvalidMagic) {
        std::cerr<<"Invalid magic was not rejected\n";++failures;
    }

    bytes[0]='C';bytes[1]='O';bytes[2]='N';bytes[3]=' ';
    put_be32(bytes,0x3A9,1);
    {
        std::ofstream out(path,std::ios::binary|std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    }
    if(wx360::storage::StfsPackageParser::parse(path).error!=wx360::storage::StfsParseError::UnsupportedVolumeType) {
        std::cerr<<"SVOD descriptor was not safely rejected\n";++failures;
    }
    std::error_code ec;std::filesystem::remove(path,ec);
    return failures==0?EXIT_SUCCESS:EXIT_FAILURE;
}
