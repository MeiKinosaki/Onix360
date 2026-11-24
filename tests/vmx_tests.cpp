#include "wx360/cpu/vmx.hpp"
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
 auto m=vmx::max_s32(s.vr[0],s.vr[1]);
 check(m.lane[0]==4U && m.lane[1]==3U,"signed vector maximum");
 s.set_fpscr_exception(0x10U);
 check((s.fpscr&0x10U)!=0,"FPSCR exception flag can be set");
 s.clear_fpscr_exceptions();
 check((s.fpscr&0x10U)==0,"FPSCR exception flags can be cleared");
 return failures==0?EXIT_SUCCESS:EXIT_FAILURE;
}
