#pragma once
#include "wx360/cpu/xenon_state.hpp"
#include <cstdint>

namespace wx360::cpu::vmx {
using Vector = XenonThreadState::VectorRegister;
[[nodiscard]] inline Vector bit_xor(const Vector& a,const Vector& b) noexcept {
 Vector r{}; for(unsigned i=0;i<4;++i) r.lane[i]=a.lane[i]^b.lane[i]; return r;
}
[[nodiscard]] inline Vector bit_and(const Vector& a,const Vector& b) noexcept {
 Vector r{}; for(unsigned i=0;i<4;++i) r.lane[i]=a.lane[i]&b.lane[i]; return r;
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
[[nodiscard]] inline Vector max_s32(const Vector& a,const Vector& b) noexcept {
 Vector r{}; for(unsigned i=0;i<4;++i) {
  const auto x=static_cast<std::int32_t>(a.lane[i]), y=static_cast<std::int32_t>(b.lane[i]);
  r.lane[i]=static_cast<std::uint32_t>(x>y?x:y);
 } return r;
}
} // namespace wx360::cpu::vmx
