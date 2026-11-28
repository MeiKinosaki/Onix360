#pragma once
#include "onix360/crypto/sha1.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace onix360::xex {
struct NormalBlockDescriptor {
    std::uint32_t block_size{};
    crypto::Sha1Digest hash{};
};
enum class NormalBlockError : std::uint8_t {
    Ok, TruncatedHeader, InvalidBlockSize, HashMismatch,
    TruncatedChunk, InvalidOutputSize, TooManyBlocks
};

[[nodiscard]] inline NormalBlockError read_normal_block_descriptor(
    std::span<const std::uint8_t> bytes, std::size_t offset,
    NormalBlockDescriptor& out) {
    if (offset > bytes.size() || bytes.size() - offset < 24)
        return NormalBlockError::TruncatedHeader;
    NormalBlockDescriptor d;
    d.block_size = (std::uint32_t(bytes[offset]) << 24) |
                   (std::uint32_t(bytes[offset+1]) << 16) |
                   (std::uint32_t(bytes[offset+2]) << 8) | bytes[offset+3];
    std::copy_n(bytes.begin()+static_cast<std::ptrdiff_t>(offset+4),20,d.hash.begin());
    out=d;
    return NormalBlockError::Ok;
}

// XEX2 Normal block stream: each descriptor authenticates the following
// block payload; payload begins with length-prefixed chunks terminated by 0.
[[nodiscard]] inline NormalBlockError unpack_normal_chunks(
    std::span<const std::uint8_t> stream,
    const NormalBlockDescriptor& first,
    std::size_t expected_output_size,
    std::vector<std::uint8_t>& output,
    bool require_exact_output = true) {
    std::vector<std::uint8_t> decoded;
    decoded.reserve(expected_output_size);
    std::size_t cursor=0;
    NormalBlockDescriptor descriptor=first;
    if (descriptor.block_size == 0) return NormalBlockError::InvalidBlockSize;
    std::size_t blocks=0;
    while(descriptor.block_size) {
        if(++blocks>65536) return NormalBlockError::TooManyBlocks;
        if(descriptor.block_size<24 || descriptor.block_size>stream.size()-cursor)
            return NormalBlockError::InvalidBlockSize;
        const auto block=stream.subspan(cursor,descriptor.block_size);
        if(crypto::sha1(block)!=descriptor.hash)
            return NormalBlockError::HashMismatch;
        NormalBlockDescriptor next;
        const auto next_result=read_normal_block_descriptor(block,0,next);
        if(next_result!=NormalBlockError::Ok) return next_result;
        std::size_t pos=24;
        bool terminated=false;
        while(pos<block.size()) {
            if(block.size()-pos<2) return NormalBlockError::TruncatedChunk;
            const std::size_t chunk=(std::size_t(block[pos])<<8)|block[pos+1];
            pos+=2;
            if(!chunk) { terminated=true; break; }
            if(chunk>block.size()-pos || decoded.size()>expected_output_size ||
               chunk>expected_output_size-decoded.size())
                return NormalBlockError::TruncatedChunk;
            decoded.insert(decoded.end(),block.begin()+static_cast<std::ptrdiff_t>(pos),
                           block.begin()+static_cast<std::ptrdiff_t>(pos+chunk));
            pos+=chunk;
        }
        if (!terminated) return NormalBlockError::TruncatedChunk;
        cursor+=descriptor.block_size;
        descriptor=next;
    }
    if(require_exact_output && decoded.size()!=expected_output_size)
        return NormalBlockError::InvalidOutputSize;
    output=std::move(decoded);
    return NormalBlockError::Ok;
}
} // namespace onix360::xex
