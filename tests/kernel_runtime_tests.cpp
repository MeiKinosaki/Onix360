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
 check(runtime.register_function_export("xboxkrnl.exe",42,8,0x200,
       &service_increment,ram),"registers handler and guest export atomically");
 check(runtime.register_function_export("xboxkrnl.exe",43,9,0x240,
       &service_increment,ram),"registers second handler and guest export atomically");
 check(!runtime.register_function_export("xboxkrnl.exe",44,7,0x280,
       &service_increment,ram),"rejects duplicate service without advertising export");
 check(runtime.emit_trampoline("xboxkrnl.exe",44,10,0x280,ram),
       "allows independent development trampoline registration");
 check(!runtime.register_function_export("xboxkrnl.exe",42,11,0x300,
       &service_increment,ram),"rejects duplicate export before committing service");
 check(!runtime.emit_trampoline("XBOXKRNL.EXE",42,7,0x300,ram),"rejects duplicate export before writing trampoline");
 std::array<std::uint8_t,12> untouched{};
 check(ram.read(0x300,untouched)==memory::MemoryResult::Ok &&
       std::all_of(untouched.begin(),untouched.end(),[](std::uint8_t b){return b==0;}),
       "duplicate trampoline rejection leaves guest memory unchanged");
 const std::array<kernel::XexImport,1> imports{{{"xboxkrnl.exe",42,0x1200,kernel::ImportKind::Function,0x1000}}};
 check(runtime.resolve_imports(imports,ram)==kernel::ImportResolveError::None,"resolves function import to registered handler");
 std::array<std::uint8_t,4> descriptor{};
 check(ram.read(0x1000,descriptor)==memory::MemoryResult::Ok &&
       descriptor==std::array<std::uint8_t,4>{0xDE,0xAD,0xC0,0xDE},
       "writes reverse-engineered function descriptor marker");
 std::array<std::uint8_t,16> thunk{};
 check(ram.read(0x1200,thunk)==memory::MemoryResult::Ok &&
       thunk[0]==0x44 && thunk[1]==0 && thunk[2]==0 && thunk[3]==0x42 &&
       thunk[4]==0x4E && thunk[5]==0x80 && thunk[6]==0 && thunk[7]==0x20,
       "writes sc 2 kernel dispatch thunk followed by blr");
 machine::XenonCore core;
 core.kernel_runtime=&runtime;
 core.state.reset(0x1200);
 core.state.gpr[3]=10;
 core.state.lr=0x3000;
 check(core.step(ram)==machine::RunResult::Executed && core.state.gpr[3]==11 &&
       core.state.pc==0x1204,
       "SC 2 dispatches host service using the guest thunk address");
 check(core.step(ram)==machine::RunResult::Executed && core.state.pc==0x3000,
       "kernel import thunk BLR returns to guest caller through LR");
 // Event wait blocks the current guest thread; signaling makes it ready again.
 cpu::XenonThreadState waiter{}, worker{}, live{};
 waiter.reset(0x500); worker.reset(0x600);
 const auto waiter_id=runtime.create_guest_thread(waiter,10);
 const auto worker_id=runtime.create_guest_thread(worker,10);
 const auto event_id=runtime.create_guest_event(true,false);
 check(waiter_id && worker_id && event_id,"creates guest threads and a manual-reset event");
 check(runtime.schedule_guest_thread(live) && live.pc==0x500,
       "starts first guest thread for synchronization test");
 check(runtime.wait_guest_event(*event_id,live)==kernel::WaitResult::Blocked &&
       live.pc==0x600,"event wait blocks current guest and schedules worker");
 check(runtime.set_guest_event(*event_id),"event signal releases queued guest waiter");
 check(runtime.terminate_guest_thread(live) && live.pc==0x500,
       "worker termination resumes event waiter");
 check(runtime.thread_scheduler().current_id()==waiter_id,
       "event waiter becomes current after signal");
 const auto sem_id=runtime.create_guest_semaphore(0,2);
 check(sem_id && runtime.wait_guest_semaphore(*sem_id,live)==kernel::WaitResult::Blocked,
       "semaphore wait queues current guest thread");
 check(runtime.release_guest_semaphore(*sem_id),"semaphore release wakes guest waiter");
 const std::array<std::uint8_t,4> guest_file_data{0xDE,0xAD,0xBE,0xEF};
 // ExAllocatePoolWithTag is registered separately and consumes the guest
 // POOL_TYPE/size/tag argument registers without invoking a real XEX.
 kernel::KernelHeap heap(ram,0x3000,0x1000);
 check(runtime.register_kernel_heap_exports(heap,20,21,0x400,0x440,ram),
       "registers bounded guest heap exports");
 check(runtime.register_kernel_heap_tagged_export(heap,22,0x480,ram),
       "registers ExAllocatePoolWithTag from verified metadata");
 cpu::XenonThreadState tagged_alloc{};
 tagged_alloc.gpr[0]=22; tagged_alloc.gpr[3]=0; tagged_alloc.gpr[4]=96;
 tagged_alloc.gpr[5]=0x54455354;
 check(runtime.dispatch(tagged_alloc) && tagged_alloc.gpr[3]>=0x3000 &&
       tagged_alloc.gpr[3]<0x4000 && heap.allocation_count()==1,
       "tagged pool allocation returns bounded guest address");
 cpu::XenonThreadState bad_pool{};
 bad_pool.gpr[0]=22; bad_pool.gpr[3]=2; bad_pool.gpr[4]=96;
 check(runtime.dispatch(bad_pool) && bad_pool.gpr[3]==0 &&
       heap.allocation_count()==1,
       "tagged pool allocation rejects unsupported pool type");
 const auto tag_stats=runtime.pool_tag_statistics(0x54455354);
 check(tag_stats.live_allocations==1 && tag_stats.live_bytes==96,
       "tracks live tagged allocation count and bytes");
 cpu::XenonThreadState tagged_free{};
 tagged_free.gpr[0]=21; tagged_free.gpr[3]=tagged_alloc.gpr[3];
 check(runtime.dispatch(tagged_free) &&
       runtime.pool_tag_statistics(0x54455354).live_allocations==0 &&
       heap.validate(),
       "ExFreePool removes tag accounting and restores heap integrity");
 check(runtime.guest_file_system().mount_file("content/default.xex",guest_file_data),
       "kernel runtime mounts frontend-provided guest content");
 const auto guest_file=runtime.guest_file_system().open("CONTENT/DEFAULT.XEX");
 std::array<std::uint8_t,4> guest_file_read{};
 const auto guest_read=guest_file ? runtime.guest_file_system().read(*guest_file,guest_file_read)
                                  : std::nullopt;
 check(guest_read && *guest_read==4 && guest_file_read==guest_file_data,
       "kernel runtime exposes bounded guest file reads");
 return failures==0?EXIT_SUCCESS:EXIT_FAILURE;
}
