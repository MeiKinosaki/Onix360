#include "wx360/kernel/kernel_runtime.hpp"
#include "wx360/machine/xenon_core.hpp"
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
 check(runtime.emit_trampoline("xboxkrnl.exe",42,7,0x200,ram),"emits guest SC trampoline");
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
