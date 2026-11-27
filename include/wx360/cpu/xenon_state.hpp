#pragma once
#include <array>
#include <cstdint>
namespace wx360::cpu {
struct XenonThreadState {
 std::array<std::uint64_t,32> gpr{};
 std::array<std::uint64_t,32> fpr{}; // IEEE-754 binary64 bit patterns
 struct alignas(16) VectorRegister { std::array<std::uint32_t,4> lane{}; };
 std::array<VectorRegister,128> vr{}; // 128 VMX registers, four 32-bit lanes each
 std::uint64_t pc{0},lr{0},ctr{0},xer{0};
 std::uint32_t cr{0};
 std::uint64_t msr{0};
 std::uint32_t fpscr{0};
 // PowerPC exception-save registers and last fault address.
 std::uint64_t srr0{0},srr1{0},dar{0};
 bool exception_pending{false};
 // Level-triggered external interrupt request; retained while MSR[EE] is clear.
 bool external_interrupt_pending{false};
 // Decrementer exception request, raised by the machine timer/scheduler.
 bool decrementer_interrupt_pending{false};
 // Single-core load-reserve/store-conditional tracking (effective address).
 bool reservation_valid{false};
 std::uint64_t reservation_address{0};
 std::uint32_t exception_vector{0};
 void raise_exception(std::uint32_t vector,std::uint64_t fault_address=0) noexcept {
  srr0=pc;srr1=msr;dar=fault_address;exception_vector=vector;exception_pending=true;msr=0;pc=vector;
 }
 void clear_exception() noexcept {exception_pending=false;exception_vector=0;}
 void request_external_interrupt() noexcept { external_interrupt_pending = true; }
 void request_decrementer_interrupt() noexcept { decrementer_interrupt_pending = true; }
 [[nodiscard]] bool take_decrementer_interrupt() noexcept {
  if (!decrementer_interrupt_pending || !external_interrupt_enabled()) return false;
  decrementer_interrupt_pending = false;
  raise_exception(0x900U);
  return true;
 }
 [[nodiscard]] bool external_interrupt_enabled() const noexcept { return (msr & 0x8000U) != 0; }
 [[nodiscard]] bool take_external_interrupt() noexcept {
  if (!external_interrupt_pending || !external_interrupt_enabled()) return false;
  external_interrupt_pending = false;
  raise_exception(0x500U);
  return true;
 }
 void set_fpscr_exception(std::uint32_t mask) noexcept { fpscr |= mask; }
 void clear_fpscr_exceptions() noexcept { fpscr &= ~0xF8U; }
 void reset(std::uint64_t entry_point=0) noexcept {gpr.fill(0);fpr.fill(0);for(auto& v:vr)v.lane.fill(0);pc=entry_point;lr=ctr=xer=msr=0;cr=fpscr=0;srr0=srr1=dar=0;clear_exception();external_interrupt_pending=false;decrementer_interrupt_pending=false;reservation_valid=false;reservation_address=0;}
 [[nodiscard]] std::uint8_t condition_field(unsigned field)const noexcept{return field>=8?0:static_cast<std::uint8_t>((cr>>(28U-field*4U))&0xFU);}
 void set_condition_field(unsigned field,std::uint8_t value)noexcept{if(field>=8)return;unsigned shift=28U-field*4U;std::uint32_t mask=0xFU<<shift;cr=(cr&~mask)|((static_cast<std::uint32_t>(value)&0xFU)<<shift);}
};
}