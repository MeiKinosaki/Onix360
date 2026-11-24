#include "wx360/memory/address_space.hpp"
#include <cstdlib>
#include <iostream>
int main() {
 using namespace wx360::memory;
 PhysicalMemory ram(2U * 1024U * 1024U); AddressSpace mmu; int failures=0;
 auto check=[&](bool ok,const char* label){if(!ok){std::cerr<<"FAIL: "<<label<<'\\n';++failures;}};
 check(mmu.map_page(0x1000,0x5000,3)==MemoryResult::Ok,"maps page");
 auto tr=mmu.translate(0x1234,Access::Read,ram);
 check(tr.result==MemoryResult::Ok && tr.physical_address==0x5234,"translates offset");
 check(mmu.translate(0x1234,Access::Execute,ram).result==MemoryResult::PermissionDenied,"checks execute permission");
 auto range=mmu.translate_range(0x1230,8,Access::Read,ram);
 check(range.result==MemoryResult::Ok && range.physical_address==0x5230,"translates full in-page range");
 check(mmu.translate_range(0x1FFC,8,Access::Read,ram).result==MemoryResult::Unmapped,"rejects access crossing guest page");
 check(mmu.translate_range(0x1234,0,Access::Read,ram).result==MemoryResult::OutOfRange,"rejects empty access range");
 check(mmu.translate(0x2234,Access::Read,ram).result==MemoryResult::Unmapped,"unmapped page faults");
 check(mmu.map_page(0x2000,0x6000,3)==MemoryResult::Ok,"maps adjacent TLB page");
 auto crossing=mmu.translate_range(0x1FF8,16,Access::Read,ram);
 check(crossing.result==MemoryResult::Ok && crossing.physical_address==0x5FF8,
       "translates contiguous access across adjacent TLB pages");
 check(mmu.map_page(0x3000,0x9000,3)==MemoryResult::Ok,"maps noncontiguous TLB page");
 check(mmu.translate_range(0x2FF8,16,Access::Read,ram).result==MemoryResult::Unmapped,
       "rejects cross-page host span when physical pages are noncontiguous");

 check(mmu.map_page(0x2001,0x6000,3)==MemoryResult::AlignmentFault,"rejects unaligned map");
 check(mmu.map_bat(0,0x80000,0x40000,128U*1024U,3)==MemoryResult::Ok,
       "maps aligned BAT-style block");
 auto bat=mmu.translate_range(0x81234,16,Access::Read,ram);
 check(bat.result==MemoryResult::Ok && bat.physical_address==0x41234,
       "BAT-style block translation preserves offset");
 check(mmu.translate(0x81234,Access::Execute,ram).result==MemoryResult::PermissionDenied,
       "BAT-style block enforces permissions");
 mmu.flush();
 check(mmu.translate(0x81234,Access::Read,ram).result==MemoryResult::Ok,
       "TLB flush preserves persistent BAT-style block mapping");
 check(mmu.map_bat(1,0x81000,0x41000,128U*1024U,3)==MemoryResult::AlignmentFault,
       "BAT-style block rejects misaligned bases");
 check(mmu.map_bat(8,0x80000,0x40000,128U*1024U,3)==MemoryResult::AlignmentFault,
       "BAT-style block rejects invalid slot");
 mmu.unmap_bat(0);
 check(mmu.translate(0x81234,Access::Read,ram).result==MemoryResult::Unmapped,
       "unmaps BAT-style block");
 mmu.unmap_page(0x1000);
 check(mmu.translate(0x1234,Access::Read,ram).result==MemoryResult::Unmapped,"unmap invalidates");
 check(mmu.map_page(0x1000,0x5000,3)==MemoryResult::Ok,"remaps page");
 mmu.flush();
 check(mmu.translate(0x1234,Access::Read,ram).result==MemoryResult::Unmapped,"flush invalidates TLB");
 return failures==0?EXIT_SUCCESS:EXIT_FAILURE;
}
