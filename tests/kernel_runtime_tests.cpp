#include "wx360/kernel/kernel_runtime.hpp"
#include "wx360/machine/xenon_core.hpp"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>

namespace {
void service_increment(wx360::cpu::XenonThreadState& s) noexcept { ++s.gpr[3]; }
}
int main() {
 using namespace wx360;
 int failures=0;
 auto check=[&](bool ok,const char* msg){if(!ok){std::cerr<<"FAIL: "<<msg<<'\n';++failures;}};
 memory::PhysicalMemory ram(0x4000);
 kernel::KernelRuntime runtime;
 check(runtime.register_service(7,&service_increment),"registers host kernel service");
 check(!runtime.register_service(7,&service_increment),"rejects duplicate service identifiers");
 cpu::XenonThreadState dispatch_state;
 dispatch_state.gpr[0]=99;
 check(!runtime.dispatch(dispatch_state),"rejects unknown kernel service identifiers");
 check(!runtime.emit_trampoline("xboxkrnl.exe",43,8,0,ram),"rejects null trampoline address");
 check(!runtime.emit_trampoline("xboxkrnl.exe",43,8,0x3FFC,ram),"rejects trampoline that exceeds guest RAM");
 check(runtime.emit_trampoline("xboxkrnl.exe",42,7,0x200,ram),"emits guest SC trampoline");
 check(!runtime.emit_trampoline("XBOXKRNL.EXE",42,7,0x300,ram),"rejects duplicate export before writing trampoline");
 std::array<std::uint8_t,12> untouched{};
 check(ram.read(0x300,untouched)==memory::MemoryResult::Ok &&
       std::all_of(untouched.begin(),untouched.end(),[](std::uint8_t b){return b==0;}),
       "duplicate trampoline rejection leaves guest memory unchanged");
 const std::array<kernel::XexImport,1> imports{{{"xboxkrnl.exe",42,0x1000,kernel::ImportKind::Function}}};
 check(runtime.resolve_imports(imports,ram)==kernel::ImportResolveError::None,"resolves function import to trampoline");
 std::array<std::uint8_t,4> thunk{};
 check(ram.read(0x1000,thunk)==memory::MemoryResult::Ok &&
       thunk[0]==0 && thunk[1]==0 && thunk[2]==2 && thunk[3]==0,
       "writes trampoline address into import thunk");
 machine::XenonCore core;
 core.kernel_runtime=&runtime;
 core.state.reset(0x200);
 core.state.gpr[3]=10;
 core.state.lr=0x3000;
 check(core.step(ram)==machine::RunResult::Executed && core.state.gpr[0]==7,
       "trampoline loads service identifier");
 check(core.step(ram)==machine::RunResult::Executed && core.state.gpr[3]==11 &&
       core.state.pc==0x208,
       "SC dispatches host service and advances to trampoline return");
 check(core.step(ram)==machine::RunResult::Executed && core.state.pc==0x3000,
       "trampoline BLR returns to guest caller through LR");
 return failures==0?EXIT_SUCCESS:EXIT_FAILURE;
}
