#pragma once
extern "C" {
#include "aes.h"
}
#include <array>
#include <cstdint>
#include <span>
#include <vector>
#include <utility>

namespace wx360::crypto {
using Aes128Key = std::array<std::uint8_t,16>;
enum class AesResult : std::uint8_t { Ok, InvalidLength };

inline void aes128_ecb_decrypt_block(
    const Aes128Key& key, std::span<std::uint8_t,16> block) {
    AES_ctx ctx{}; AES_init_ctx(&ctx,key.data());
    AES_ECB_decrypt(&ctx,block.data());
}

[[nodiscard]] inline AesResult aes128_cbc_decrypt(
    const Aes128Key& key, const Aes128Key& iv,
    std::span<const std::uint8_t> input, std::vector<std::uint8_t>& output) {
    if(input.empty() || input.size()%16u) return AesResult::InvalidLength;
    std::vector<std::uint8_t> decoded(input.begin(),input.end());
    AES_ctx ctx{}; AES_init_ctx_iv(&ctx,key.data(),iv.data());
    AES_CBC_decrypt_buffer(&ctx,decoded.data(),decoded.size());
    output=std::move(decoded); return AesResult::Ok;
}

enum class XexKeySet : std::uint8_t { Retail, Devkit };
[[nodiscard]] inline Aes128Key xex_master_key(XexKeySet set) noexcept {
    if (set == XexKeySet::Retail) return {0x20,0xB1,0x85,0xA5,0x9D,0x28,0xFD,0xC3,0x40,0x58,0x3F,0xBB,0x08,0x96,0xBF,0x91};
    return {};
}

// XEX2 Normal encryption starts with a zero CBC chaining value.
[[nodiscard]] inline AesResult aes128_xex_cbc_decrypt(
    const Aes128Key& key, std::span<const std::uint8_t> input,
    std::vector<std::uint8_t>& output) {
    const Aes128Key zero_iv{};
    return aes128_cbc_decrypt(key, zero_iv, input, output);
}

// XEX stores the session key encrypted under a console-family master key.
// This routine only unwraps the 16-byte field; callers must select the
// correct retail/devkit key and validate the resulting image separately.
[[nodiscard]] inline Aes128Key unwrap_xex_session_key(
    const Aes128Key& master_key, const Aes128Key& encrypted_session_key) {
    auto result=encrypted_session_key;
    aes128_ecb_decrypt_block(master_key,std::span<std::uint8_t,16>(result));
    return result;
}
[[nodiscard]] inline Aes128Key unwrap_xex_session_key(
    XexKeySet set, const Aes128Key& encrypted_session_key) {
    return unwrap_xex_session_key(xex_master_key(set), encrypted_session_key);
}
} // namespace wx360::crypto
