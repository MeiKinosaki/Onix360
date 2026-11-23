#pragma once
#include <array>
#include <cstdint>
namespace wx360::cpu {
struct XenonThreadState {
 std::array<std::uint64_t,32> gpr{};
 std::uint64_t pc{0},lr{0},ctr{0},xer{0};
 std::uint32_t cr{0};
 std::uint64_t msr{0};
 std::uint32_t fpscr{0};
 // PowerPC exception-save registers and last fault address.
 std::uint64_t srr0{0},srr1{0},dar{0};
 bool exception_pending{false};
 std::uint32_t exception_vector{0};
 void raise_exception(std::uint32_t vector,std::uint64_t fault_address=0) noexcept {
  srr0=pc;srr1=msr;dar=fault_address;exception_vector=vector;exception_pending=true;msr=0;pc=vector;
 }
 void clear_exception() noexcept {exception_pending=false;exception_vector=0;}
 void reset(std::uint64_t entry_point=0) noexcept {gpr.fill(0);pc=entry_point;lr=ctr=xer=msr=0;cr=fpscr=0;srr0=srr1=dar=0;clear_exception();}
 [[nodiscard]] std::uint8_t condition_field(unsigned field)const noexcept{return field>=8?0:static_cast<std::uint8_t>((cr>>(28U-field*4U))&0xFU);}
 void set_condition_field(unsigned field,std::uint8_t value)noexcept{if(field>=8)return;unsigned shift=28U-field*4U;std::uint32_t mask=0xFU<<shift;cr=(cr&~mask)|((static_cast<std::uint32_t>(value)&0xFU)<<shift);}
};
}