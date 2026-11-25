#include "wx360/cpu/vmx.hpp"
#include <bit>
#include <cstdlib>
#include <iostream>
int main() {
 using namespace wx360::cpu;
 XenonThreadState s; s.reset();
 int failures=0;
 auto check=[&](bool ok,const char* msg){if(!ok){std::cerr<<"FAIL: "<<msg<<'\n';++failures;}};
 check(s.vr.size()==128,"state contains 128 VMX registers");
 check(sizeof(s.vr[0])==16,"each VMX register is 128 bits");
 s.vr[0].lane={1U,2U,0xFFFFFFFFU,8U};
 s.vr[1].lane={4U,3U,1U,2U};
 auto x=vmx::bit_xor(s.vr[0],s.vr[1]);
 check(x.lane[0]==5U && x.lane[1]==1U,"vector XOR");
 auto a=vmx::add_u32(s.vr[0],s.vr[1]);
 check(a.lane[0]==5U && a.lane[2]==0U,"vector add wraps modulo 32 bits");
 s.vr[2].lane={std::bit_cast<std::uint32_t>(1.5F),std::bit_cast<std::uint32_t>(2.0F),std::bit_cast<std::uint32_t>(-1.0F),std::bit_cast<std::uint32_t>(4.0F)};
 s.vr[3].lane={std::bit_cast<std::uint32_t>(2.5F),std::bit_cast<std::uint32_t>(3.0F),std::bit_cast<std::uint32_t>(5.0F),std::bit_cast<std::uint32_t>(0.5F)};
 auto vf=vmx::add_f32(s.vr[2],s.vr[3]);
 check(std::bit_cast<float>(vf.lane[0])==4.0F && std::bit_cast<float>(vf.lane[3])==4.5F,"VMX vector float add");
 auto vm=vmx::mul_f32(s.vr[2],s.vr[3]);
 check(std::bit_cast<float>(vm.lane[0])==3.75F && std::bit_cast<float>(vm.lane[2])==-5.0F,"VMX vector float multiply");
 auto m=vmx::max_s32(s.vr[0],s.vr[1]);
 check(m.lane[0]==4U && m.lane[1]==3U,"signed vector maximum");
 auto ac=vmx::bit_andc(s.vr[0],s.vr[1]);
 check(ac.lane[0]==1U && ac.lane[1]==0U,"VMX AND-complement");
 auto no=vmx::bit_nor(s.vr[0],s.vr[1]);
 check(no.lane[0]==~5U,"VMX NOR");
 s.vr[4].lane={0xFF00FF00U,0x01020304U,0x00000000U,0xFFFFFFFFU};
 s.vr[5].lane={0x01010101U,0x01010101U,0xFFFFFFFFU,0x01010101U};
 auto bytes=vmx::add_u8(s.vr[4],s.vr[5]);
 check(bytes.lane[0]==0x00010001U && bytes.lane[1]==0x02030405U,
       "VMX unsigned byte addition wraps each byte lane");
 s.vr[6].lane={0x000100FFU,0x807F0102U,0xFFFFFFFFU,0x00000000U};
 s.vr[7].lane={0x00020001U,0x01020304U,0x01010101U,0xFFFFFFFFU};
 auto byte_sub=vmx::sub_u8(s.vr[6],s.vr[7]);
 check(byte_sub.lane[0]==0x00FF00FEU && byte_sub.lane[1]==0x7F7DFEFEU,
       "VMX unsigned byte subtraction wraps independently per byte");
 auto byte_max=vmx::max_u8(s.vr[6],s.vr[7]);
 check(byte_max.lane[0]==0x000200FFU && byte_max.lane[1]==0x807F0304U &&
       byte_max.lane[2]==0xFFFFFFFFU && byte_max.lane[3]==0xFFFFFFFFU,
       "VMX unsigned byte maximum compares each byte independently");
 auto byte_add_sat=vmx::add_sat_u8(s.vr[6],s.vr[7]);
 check(byte_add_sat.lane[0]==0x000300FFU && byte_add_sat.lane[1]==0x81810406U &&
       byte_add_sat.lane[2]==0xFFFFFFFFU && byte_add_sat.lane[3]==0xFFFFFFFFU,
       "VMX unsigned byte saturating add clamps each byte at 255");
 auto byte_sub_sat=vmx::sub_sat_u8(s.vr[6],s.vr[7]);
 check(byte_sub_sat.lane[0]==0x000000FEU && byte_sub_sat.lane[1]==0x7F7D0000U &&
       byte_sub_sat.lane[2]==0xFEFEFEFEU && byte_sub_sat.lane[3]==0x00000000U,
       "VMX unsigned byte saturating subtract clamps each byte at zero");
 s.vr[8].lane={0x7F80FE01U,0x80807F01U,0x01020304U,0xFFFFFFFFU};
 s.vr[9].lane={0x0101FFFEU,0xFF0181FFU,0x01010101U,0x00000001U};
 auto signed_add_sat=vmx::add_sat_s8(s.vr[8],s.vr[9]);
 check(signed_add_sat.lane[0]==0x7F81FDFFU &&
       signed_add_sat.lane[1]==0x80810000U,
       "VMX signed byte saturating add clamps to signed 8-bit range");
 auto signed_sub_sat=vmx::sub_sat_s8(s.vr[8],s.vr[9]);
 check(signed_sub_sat.lane[0]==0x7E80FF03U &&
       signed_sub_sat.lane[1]==0x81807F02U,
       "VMX signed byte saturating subtract clamps to signed 8-bit range");
 auto signed_max=vmx::max_s8(s.vr[8],s.vr[9]);
 check(signed_max.lane[0]==0x7F01FF01U &&
       signed_max.lane[1]==0xFF017F01U,
       "VMX signed byte maximum compares signed lanes independently");



 s.set_fpscr_exception(0x10U);
 check((s.fpscr&0x10U)!=0,"FPSCR exception flag can be set");
 s.clear_fpscr_exceptions();
 check((s.fpscr&0x10U)==0,"FPSCR exception flags can be cleared");
 return failures==0?EXIT_SUCCESS:EXIT_FAILURE;
}
