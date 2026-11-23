#pragma once
#include "wx360/xex/xex_header.hpp"
#include "wx360/kernel/xex_import_resolver.hpp"
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace wx360::xex {
struct ImportLibraryRecord {
    std::string name;
    std::uint32_t library_id{}, version{}, minimum_version{};
    std::uint16_t version_name_index{}, import_count{};
    std::vector<std::uint32_t> import_thunks;
};
enum class ImportTableError : std::uint8_t {
    Ok, MissingHeader, Truncated, InvalidSize, InvalidStringTable,
    InvalidLibrary, TooManyLibraries, InvalidName
};
struct ImportTable {
    std::vector<ImportLibraryRecord> libraries;
};

[[nodiscard]] inline ImportTableError parse_import_table(
    std::span<const std::uint8_t> file, std::uint32_t offset,
    ImportTable& output) {
    auto be16=[&](std::size_t p){return static_cast<std::uint16_t>((file[p]<<8)|file[p+1]);};
    auto be32=[&](std::size_t p){return (std::uint32_t(file[p])<<24)|(std::uint32_t(file[p+1])<<16)|(std::uint32_t(file[p+2])<<8)|file[p+3];};
    if(offset>file.size() || file.size()-offset<12) return ImportTableError::Truncated;
    const auto total=be32(offset), strings_size=be32(offset+4), strings_count=be32(offset+8);
    if(total<12 || total>file.size()-offset || strings_size<4 ||
       strings_size>total-12 || strings_count>256)
        return ImportTableError::InvalidSize;
    const auto strings_begin=std::size_t(offset)+12;
    const auto strings_end=strings_begin+strings_size;
    std::vector<std::string> names;
    std::size_t cursor=strings_begin;
    for(std::uint32_t i=0;i<strings_count;++i) {
        if(cursor>=strings_end) return ImportTableError::InvalidStringTable;
        auto end=cursor;
        while(end<strings_end && file[end]) ++end;
        if(end==strings_end) return ImportTableError::InvalidStringTable;
        if(end==cursor || end-cursor>128) return ImportTableError::InvalidName;
        names.emplace_back(reinterpret_cast<const char*>(file.data()+cursor),end-cursor);
        cursor=end+1;
        cursor=(cursor+3u)&~std::size_t(3u);
        if(cursor>strings_end && i+1<strings_count) return ImportTableError::InvalidStringTable;
    }
    ImportTable parsed;
    cursor=strings_begin+strings_size;
    while(cursor<std::size_t(offset)+total) {
        if(std::size_t(offset)+total-cursor<0x28) return ImportTableError::InvalidLibrary;
        const auto size=be32(cursor);
        if(size<0x28 || size>std::size_t(offset)+total-cursor || (size-0x28)%4)
            return ImportTableError::InvalidLibrary;
        const auto name_index=be16(cursor+0x24);
        const auto count=be16(cursor+0x26);
        if(name_index>=names.size() || count>(size-0x28)/4)
            return ImportTableError::InvalidLibrary;
        ImportLibraryRecord lib;
        lib.name=names[name_index]; lib.library_id=be32(cursor+0x18);
        lib.version=be32(cursor+0x1C); lib.minimum_version=be32(cursor+0x20);
        lib.version_name_index=name_index; lib.import_count=count;
        lib.import_thunks.reserve(count);
        for(std::uint16_t i=0;i<count;++i) lib.import_thunks.push_back(be32(cursor+0x28+i*4));
        parsed.libraries.push_back(std::move(lib));
        if(parsed.libraries.size()>64) return ImportTableError::TooManyLibraries;
        cursor+=size;
    }
    output=std::move(parsed);
    return ImportTableError::Ok;
}
} // namespace wx360::xex
