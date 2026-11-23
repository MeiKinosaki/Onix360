#include "wx360/storage/stfs_package.hpp"
#include "wx360/storage/content_loader.hpp"

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
    std::vector<std::uint8_t> bytes(0xD000,0);
    bytes[0]='C';bytes[1]='O';bytes[2]='N';bytes[3]=' ';
    put_be32(bytes,0x340,0x971A);
    put_be32(bytes,0x344,0x00007000);
    put_be32(bytes,0x348,2);
    put_be64(bytes,0x34C,0x12345678);
    put_be32(bytes,0x360,0x4D5307E6);
    bytes[0x379]=0x24;
    bytes[0x37B]=0x01; // read-only STFS: one backing block per hash table
    put_le16(bytes,0x379+3,1);
    put_le24(bytes,0x379+5,0);
    put_be32(bytes,0x379+0x1C,10);
    put_be32(bytes,0x379+0x20,2);
    put_be32(bytes,0x39D,1);
    put_be64(bytes,0x3A1,0x12345678);
    put_utf16be(bytes,0x1611,"Publisher");
    put_utf16be(bytes,0x1691,"Test Game");
    const std::size_t table = 0xB000;
    const char* filename = "default.xex";
    for (std::size_t i=0; filename[i]; ++i) bytes[table+i]=static_cast<std::uint8_t>(filename[i]);
    bytes[table+0x28]=static_cast<std::uint8_t>(0x40u | 11u);
    put_le24(bytes,table+0x29,1);
    put_le24(bytes,table+0x2C,1);
    put_le24(bytes,table+0x2F,1);
    bytes[table+0x32]=0xFF; bytes[table+0x33]=0xFF;
    put_be32(bytes,table+0x34,0x1800);
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
    auto file_table=wx360::storage::StfsPackageParser::read_file_table(path,parsed.header);
    if(!file_table || file_table.entries.size()!=1 ||
       file_table.entries[0].name!="default.xex" ||
       file_table.entries[0].parent_directory_index!=0xFFFFu ||
       file_table.entries[0].file_size!=0x1800u ||
       file_table.entries[0].starting_block!=1u) {
        std::cerr<<"Read-only STFS file table was not parsed correctly\\n";++failures;
    }

    // Exercise a two-block directory table and the level-0 next-block link.
    put_le16(bytes,0x379+3,2);
    put_be32(bytes,0xA014,0x80000001u); // block 0 -> block 1, allocated
    const std::size_t second_table=0xC000;
    const char* second_name="second.bin";
    for(std::size_t i=0;second_name[i];++i)
        bytes[second_table+i]=static_cast<std::uint8_t>(second_name[i]);
    bytes[second_table+0x28]=static_cast<std::uint8_t>(0x40u | 10u);
    put_le24(bytes,second_table+0x29,1);
    put_le24(bytes,second_table+0x2C,1);
    put_le24(bytes,second_table+0x2F,2);
    bytes[second_table+0x32]=0xFF; bytes[second_table+0x33]=0xFF;
    put_be32(bytes,second_table+0x34,0x1000);
    {
        std::ofstream out(path,std::ios::binary|std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    }
    const auto chained_header=wx360::storage::StfsPackageParser::parse(path);
    const auto chained_table=wx360::storage::StfsPackageParser::read_file_table(path,chained_header.header);
    if(!chained_header || !chained_table || chained_table.entries.size()!=2 ||
       chained_table.entries[1].name!="second.bin") {
        std::cerr<<"STFS file table block chain was not followed\\n";++failures;
    }

    // Read an individual file through its contiguous data-block mapping.
    wx360::storage::StfsParseError read_error{};
    const auto extracted_bytes = wx360::storage::StfsPackageParser::read_file(
        path, chained_header.header, chained_table.entries[1], read_error);
    if (read_error != wx360::storage::StfsParseError::None ||
        std::string(extracted_bytes.begin(), extracted_bytes.end()) != "second.bin") {
        std::cerr << "STFS file data block was not read correctly\\n"; ++failures;
    }
    const auto extract_dir = std::filesystem::temp_directory_path() / "wx360_stfs_extract_test";
    std::error_code extract_ec;
    std::filesystem::remove_all(extract_dir, extract_ec);
    if (!wx360::storage::StfsPackageParser::extract_file(
            path, chained_header.header, chained_table.entries[1], extract_dir, read_error)) {
        std::cerr << "Safe STFS single-file extraction failed\\n"; ++failures;
    } else {
        std::ifstream extracted(extract_dir / "second.bin", std::ios::binary);
        std::string extracted_text((std::istreambuf_iterator<char>(extracted)), {});
        if (extracted_text != "second.bin") {
            std::cerr << "Extracted STFS file contents mismatch\\n"; ++failures;
        }
    }
    auto unsafe_entry = chained_table.entries[1];
    unsafe_entry.name = "../escape.bin";
    if (wx360::storage::StfsPackageParser::extract_file(
            path, chained_header.header, unsafe_entry, extract_dir, read_error)) {
        std::cerr << "STFS path traversal was not rejected\\n"; ++failures;
    }
    std::filesystem::remove_all(extract_dir, extract_ec);

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
