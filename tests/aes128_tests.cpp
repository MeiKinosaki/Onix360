#include "wx360/crypto/aes128.hpp"
#include "wx360/xex/xex_image_loader.hpp"
#include <array>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

int main(){
    using namespace wx360::crypto;
    const Aes128Key key{0x2b,0x7e,0x15,0x16,0x28,0xae,0xd2,0xa6,0xab,0xf7,0x15,0x88,0x09,0xcf,0x4f,0x3c};
    const Aes128Key iv{0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f};
    const Aes128Key cipher{0x76,0x49,0xab,0xac,0x81,0x19,0xb2,0x46,0xce,0xe9,0x8e,0x9b,0x12,0xe9,0x19,0x7d};
    const Aes128Key plain{0x6b,0xc1,0xbe,0xe2,0x2e,0x40,0x9f,0x96,0xe9,0x3d,0x7e,0x11,0x73,0x93,0x17,0x2a};
    // ECB known-answer uses a separate NIST ciphertext.
    const Aes128Key ecb_cipher{0x3a,0xd7,0x7b,0xb4,0x0d,0x7a,0x36,0x60,0xa8,0x9e,0xca,0xf3,0x24,0x66,0xef,0x97};
    auto ecb=ecb_cipher;
    aes128_ecb_decrypt_block(key,std::span<std::uint8_t,16>(ecb));
    if(ecb!=plain){std::cerr<<"AES-128 ECB vector failed\\n";return EXIT_FAILURE;}
    const auto unwrapped=unwrap_xex_session_key(key,ecb_cipher);
    if(unwrapped!=plain){
        std::cerr<<"AES session-key ECB unwrap vector failed\\n";return EXIT_FAILURE;
    }
    std::vector<std::uint8_t> decoded;
    if(aes128_cbc_decrypt(key,iv,cipher,decoded)!=AesResult::Ok ||
       decoded.size()!=16 || !std::equal(decoded.begin(),decoded.end(),plain.begin())){
        std::cerr<<"AES-128 CBC vector failed\\n";return EXIT_FAILURE;
    }
    const Aes128Key zero_iv_cipher{
        0x3a,0xd7,0x7b,0xb4,0x0d,0x7a,0x36,0x60,
        0xa8,0x9e,0xca,0xf3,0x24,0x66,0xef,0x97};
    if(aes128_xex_cbc_decrypt(key,zero_iv_cipher,decoded)!=AesResult::Ok ||
       decoded.size()!=16 || !std::equal(decoded.begin(),decoded.end(),plain.begin())){
        std::cerr<<"XEX zero-IV CBC known-answer vector failed\\n";return EXIT_FAILURE;
    }
    // Independent OpenSSL AES-128-ECB known-answer vectors for the public
    // XEX2 retail and devkit master keys, unwrapping the same session key.
    const Aes128Key session_key{
        0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,
        0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff};
    const Aes128Key retail_wrapped{
        0x95,0x41,0x31,0xbc,0x3a,0x1b,0x4f,0x10,
        0x59,0x70,0xcd,0x62,0xfe,0xc5,0xb5,0xb5};
    const Aes128Key devkit_wrapped{
        0xc8,0xa3,0x31,0xff,0x8e,0xdd,0x3d,0xb1,
        0x75,0xe1,0x54,0x5d,0xbe,0xfb,0x76,0x0b};
    if (unwrap_xex_session_key(XexKeySet::Retail, retail_wrapped) != session_key ||
        unwrap_xex_session_key(XexKeySet::Devkit, devkit_wrapped) != session_key ||
        !xex_master_key_available(XexKeySet::Retail) ||
        !xex_master_key_available(XexKeySet::Devkit)) {
        std::cerr << "XEX2 Retail/Devkit master-key unwrap vectors failed\\n";
        return EXIT_FAILURE;
    }
    wx360::xex::SecurityInfo devkit_security{};
    devkit_security.encrypted_image_key = devkit_wrapped;
    if (wx360::xex::XexImageLoader::decrypt_xex_normal_payload(
            devkit_security, XexKeySet::Devkit, zero_iv_cipher, decoded) != AesResult::Ok) {
        std::cerr << "XEX Devkit payload decrypt path failed\\n";
        return EXIT_FAILURE;
    }
    std::array<std::uint8_t,15> invalid{};
    if(aes128_cbc_decrypt(key,iv,invalid,decoded)!=AesResult::InvalidLength){
        std::cerr<<"AES CBC accepted non-block-aligned input\\n";return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
