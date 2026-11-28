#pragma once
#include "onix360/cpu/xenon_state.hpp"
#include <bit>
#include <cmath>
#include <cstdint>

namespace onix360::cpu::vmx {
using Vector = XenonThreadState::VectorRegister;
[[nodiscard]] inline Vector bit_xor(const Vector& a,const Vector& b) noexcept {
 Vector r{}; for(unsigned i=0;i<4;++i) r.lane[i]=a.lane[i]^b.lane[i]; return r;
}
[[nodiscard]] inline Vector bit_and(const Vector& a,const Vector& b) noexcept {
 Vector r{}; for(unsigned i=0;i<4;++i) r.lane[i]=a.lane[i]&b.lane[i]; return r;
}
[[nodiscard]] inline Vector bit_andc(const Vector& a,const Vector& b) noexcept {
 Vector r{}; for(unsigned i=0;i<4;++i) r.lane[i]=a.lane[i]&~b.lane[i]; return r;
}
[[nodiscard]] inline Vector bit_nor(const Vector& a,const Vector& b) noexcept {
 Vector r{}; for(unsigned i=0;i<4;++i) r.lane[i]=~(a.lane[i]|b.lane[i]); return r;
}
[[nodiscard]] inline Vector add_u8(const Vector& a,const Vector& b) noexcept {
 Vector r{}; for(unsigned i=0;i<16;++i) {
  const unsigned shift=(i%4U)*8U, lane=i/4U;
  const auto x=(a.lane[lane]>>shift)&0xFFU, y=(b.lane[lane]>>shift)&0xFFU;
  r.lane[lane] |= ((x+y)&0xFFU)<<shift;
 } return r;
}
[[nodiscard]] inline Vector sub_u8(const Vector& a,const Vector& b) noexcept {
 Vector r{}; for(unsigned i=0;i<16;++i) {
  const unsigned shift=(i%4U)*8U, lane=i/4U;
  const auto x=(a.lane[lane]>>shift)&0xFFU, y=(b.lane[lane]>>shift)&0xFFU;
  r.lane[lane] |= ((x-y)&0xFFU)<<shift;
 } return r;
}
[[nodiscard]] inline Vector add_sat_u8(const Vector& a,const Vector& b) noexcept {
 Vector r{}; for(unsigned i=0;i<16;++i) {
  const unsigned shift=(i%4U)*8U, lane=i/4U;
  const auto x=(a.lane[lane]>>shift)&0xFFU, y=(b.lane[lane]>>shift)&0xFFU;
  const auto sum=x+y;
  r.lane[lane] |= (sum>0xFFU?0xFFU:sum)<<shift;
 } return r;
}
[[nodiscard]] inline Vector sub_sat_u8(const Vector& a,const Vector& b) noexcept {
 Vector r{}; for(unsigned i=0;i<16;++i) {
  const unsigned shift=(i%4U)*8U, lane=i/4U;
  const auto x=(a.lane[lane]>>shift)&0xFFU, y=(b.lane[lane]>>shift)&0xFFU;
  r.lane[lane] |= (x<y?0U:x-y)<<shift;
 } return r;
}
[[nodiscard]] inline Vector add_sat_s8(const Vector& a,const Vector& b) noexcept {
 Vector r{}; for(unsigned i=0;i<16;++i) {
  const unsigned shift=(i%4U)*8U, lane=i/4U;
  const auto x=static_cast<std::int8_t>((a.lane[lane]>>shift)&0xFFU);
  const auto y=static_cast<std::int8_t>((b.lane[lane]>>shift)&0xFFU);
  const int sum=static_cast<int>(x)+static_cast<int>(y);
  const auto clamped=static_cast<std::int8_t>(sum>127?127:(sum< -128?-128:sum));
  r.lane[lane] |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(clamped))<<shift;
 } return r;
}
[[nodiscard]] inline Vector sub_sat_s8(const Vector& a,const Vector& b) noexcept {
 Vector r{}; for(unsigned i=0;i<16;++i) {
  const unsigned shift=(i%4U)*8U, lane=i/4U;
  const auto x=static_cast<std::int8_t>((a.lane[lane]>>shift)&0xFFU);
  const auto y=static_cast<std::int8_t>((b.lane[lane]>>shift)&0xFFU);
  const int difference=static_cast<int>(x)-static_cast<int>(y);
  const auto clamped=static_cast<std::int8_t>(difference>127?127:(difference< -128?-128:difference));
  r.lane[lane] |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(clamped))<<shift;
 } return r;
}
[[nodiscard]] inline Vector max_s8(const Vector& a,const Vector& b) noexcept {
 Vector r{}; for(unsigned i=0;i<16;++i) {
  const unsigned shift=(i%4U)*8U, lane=i/4U;
  const auto x=static_cast<std::int8_t>((a.lane[lane]>>shift)&0xFFU);
  const auto y=static_cast<std::int8_t>((b.lane[lane]>>shift)&0xFFU);
  const auto maximum=x>y?x:y;
  r.lane[lane] |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(maximum))<<shift;
 } return r;
}
[[nodiscard]] inline Vector min_s8(const Vector& a,const Vector& b) noexcept {
 Vector r{}; for(unsigned i=0;i<16;++i) {
  const unsigned shift=(i%4U)*8U, lane=i/4U;
  const auto x=static_cast<std::int8_t>((a.lane[lane]>>shift)&0xFFU);
  const auto y=static_cast<std::int8_t>((b.lane[lane]>>shift)&0xFFU);
  const auto minimum=x<y?x:y;
  r.lane[lane] |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(minimum))<<shift;
 } return r;
}
[[nodiscard]] inline Vector min_u8(const Vector& a,const Vector& b) noexcept {
 Vector r{}; for(unsigned i=0;i<16;++i) {
  const unsigned shift=(i%4U)*8U, lane=i/4U;
  const auto x=(a.lane[lane]>>shift)&0xFFU, y=(b.lane[lane]>>shift)&0xFFU;
  r.lane[lane] |= (x<y?x:y)<<shift;
 } return r;
}
[[nodiscard]] inline Vector max_u8(const Vector& a,const Vector& b) noexcept {
 Vector r{}; for(unsigned i=0;i<16;++i) {
  const unsigned shift=(i%4U)*8U, lane=i/4U;
  const auto x=(a.lane[lane]>>shift)&0xFFU, y=(b.lane[lane]>>shift)&0xFFU;
  r.lane[lane] |= (x>y?x:y)<<shift;
 } return r;
}
[[nodiscard]] inline Vector bit_or(const Vector& a,const Vector& b) noexcept {
 Vector r{}; for(unsigned i=0;i<4;++i) r.lane[i]=a.lane[i]|b.lane[i]; return r;
}
[[nodiscard]] inline Vector add_u32(const Vector& a,const Vector& b) noexcept {
 Vector r{}; for(unsigned i=0;i<4;++i) r.lane[i]=a.lane[i]+b.lane[i]; return r;
}
[[nodiscard]] inline Vector sub_u32(const Vector& a,const Vector& b) noexcept {
 Vector r{}; for(unsigned i=0;i<4;++i) r.lane[i]=a.lane[i]-b.lane[i]; return r;
}
[[nodiscard]] inline Vector min_s32(const Vector& a,const Vector& b) noexcept {
 Vector r{}; for(unsigned i=0;i<4;++i) {
  const auto x=static_cast<std::int32_t>(a.lane[i]), y=static_cast<std::int32_t>(b.lane[i]);
  r.lane[i]=static_cast<std::uint32_t>(x<y?x:y);
 } return r;
}
[[nodiscard]] inline Vector min_u32(const Vector& a,const Vector& b) noexcept {
 Vector r{}; for(unsigned i=0;i<4;++i) r.lane[i]=a.lane[i]<b.lane[i]?a.lane[i]:b.lane[i];
 return r;
}
[[nodiscard]] inline Vector max_s32(const Vector& a,const Vector& b) noexcept {
 Vector r{}; for(unsigned i=0;i<4;++i) {
  const auto x=static_cast<std::int32_t>(a.lane[i]), y=static_cast<std::int32_t>(b.lane[i]);
  r.lane[i]=static_cast<std::uint32_t>(x>y?x:y);
 } return r;
}
[[nodiscard]] inline Vector add_f32(const Vector& a,const Vector& b) noexcept {
 Vector r{}; for(unsigned i=0;i<4;++i) {
  const float x=std::bit_cast<float>(a.lane[i]), y=std::bit_cast<float>(b.lane[i]);
  r.lane[i]=std::bit_cast<std::uint32_t>(x+y);
 } return r;
}
[[nodiscard]] inline Vector sub_f32(const Vector& a,const Vector& b) noexcept {
 Vector r{}; for(unsigned i=0;i<4;++i) {
  const float x=std::bit_cast<float>(a.lane[i]), y=std::bit_cast<float>(b.lane[i]);
  r.lane[i]=std::bit_cast<std::uint32_t>(x-y);
 } return r;
}
[[nodiscard]] inline Vector mul_f32(const Vector& a,const Vector& b) noexcept {
 Vector r{}; for(unsigned i=0;i<4;++i) {
  const float x=std::bit_cast<float>(a.lane[i]), y=std::bit_cast<float>(b.lane[i]);
  r.lane[i]=std::bit_cast<std::uint32_t>(x*y);
 } return r;
}
[[nodiscard]] inline Vector max_f32(const Vector& a,const Vector& b) noexcept {
 Vector r{}; for(unsigned i=0;i<4;++i) {
  const float x=std::bit_cast<float>(a.lane[i]), y=std::bit_cast<float>(b.lane[i]);
  r.lane[i]=std::bit_cast<std::uint32_t>(std::fmax(x,y));
 } return r;
}
} // namespace onix360::cpu::vmx
