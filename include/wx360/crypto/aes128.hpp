#pragma once
#include "aes.h"
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace wx360::crypto {
using Aes128Key = std::array<std::uint8_t,16>;
enum class AesResult : std::uint8_t { Ok, InvalidLength };

[[nodiscard]] inline void aes128_ecb_decrypt_block(
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

// XEX stores the session key encrypted under a console-family master key.
// This routine only unwraps the 16-byte field; callers must select the
// correct retail/devkit key and validate the resulting image separately.
[[nodiscard]] inline Aes128Key unwrap_xex_session_key(
    const Aes128Key& master_key, const Aes128Key& encrypted_session_key) {
    auto result=encrypted_session_key;
    aes128_ecb_decrypt_block(master_key,std::span<std::uint8_t,16>(result));
    return result;
}
} // namespace wx360::crypto
