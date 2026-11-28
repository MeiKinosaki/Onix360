#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace onix360::crypto {
using Sha1Digest = std::array<std::uint8_t, 20>;

[[nodiscard]] inline Sha1Digest sha1(std::span<const std::uint8_t> input) {
    std::vector<std::uint8_t> data(input.begin(), input.end());
    const std::uint64_t bit_length = static_cast<std::uint64_t>(data.size()) * 8u;
    data.push_back(0x80);
    while ((data.size() % 64u) != 56u) data.push_back(0);
    for (int i = 7; i >= 0; --i)
        data.push_back(static_cast<std::uint8_t>(bit_length >> (i * 8)));

    std::uint32_t h0=0x67452301u, h1=0xEFCDAB89u, h2=0x98BADCFEu,
                  h3=0x10325476u, h4=0xC3D2E1F0u;
    for (std::size_t base=0; base<data.size(); base+=64) {
        std::uint32_t w[80]{};
        for (unsigned i=0;i<16;++i) {
            const auto p=base+i*4;
            w[i]=(std::uint32_t(data[p])<<24)|(std::uint32_t(data[p+1])<<16)|
                 (std::uint32_t(data[p+2])<<8)|data[p+3];
        }
        for (unsigned i=16;i<80;++i) {
            const auto x=w[i-3]^w[i-8]^w[i-14]^w[i-16];
            w[i]=(x<<1)|(x>>31);
        }
        auto a=h0,b=h1,c=h2,d=h3,e=h4;
        for(unsigned i=0;i<80;++i) {
            std::uint32_t f,k;
            if(i<20){f=(b&c)|((~b)&d);k=0x5A827999u;}
            else if(i<40){f=b^c^d;k=0x6ED9EBA1u;}
            else if(i<60){f=(b&c)|(b&d)|(c&d);k=0x8F1BBCDCu;}
            else {f=b^c^d;k=0xCA62C1D6u;}
            const auto rot=(a<<5)|(a>>27);
            const auto temp=rot+f+e+k+w[i];
            e=d;d=c;c=(b<<30)|(b>>2);b=a;a=temp;
        }
        h0+=a;h1+=b;h2+=c;h3+=d;h4+=e;
    }
    Sha1Digest out{};
    const std::uint32_t words[5]={h0,h1,h2,h3,h4};
    for(unsigned i=0;i<5;++i) for(unsigned j=0;j<4;++j)
        out[i*4+j]=static_cast<std::uint8_t>(words[i]>>(24-8*j));
    return out;
}
} // namespace onix360::crypto
