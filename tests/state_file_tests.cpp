#include "wx360/state/state_file.hpp"
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <vector>
#include <fstream>
namespace { int failures=0; void check(bool ok,const char*m){if(!ok){std::cerr<<"FAIL: "<<m<<'\n';++failures;}} }
int main(){
 using namespace wx360;
 state::Snapshot source; source.cpu.pc=0x123456789ABCDEF0ULL;source.cpu.gpr[3]=0xDEADBEEF12345678ULL;
 source.cpu.lr=0x1000;source.cpu.ctr=77;source.cpu.xer=0x20000000;source.cpu.cr=0xA1234567;
 source.cpu.msr=0x8000000000000000ULL;source.cpu.fpscr=0xCAFEBABE;
 source.cpu.fpr[5]=0x3FF0000000000000ULL;source.cpu.vr[127].lane={1U,2U,3U,4U};
 source.cpu.segment_registers[2]=0x81234567U;source.cpu.srr0=0x1234;source.cpu.srr1=0x5678;source.cpu.dar=0x9ABC;
 source.cpu.exception_pending=true;source.cpu.external_interrupt_pending=true;source.cpu.decrementer_interrupt_pending=true;
 source.cpu.reservation_valid=true;source.cpu.reservation_address=0x82001000ULL;
 source.cpu.decrementer=0x10203040U;source.cpu.timebase_ticks=0x1122334455667788ULL;source.cpu.exception_vector=0x500U;
 source.ram={0,1,2,3,0xFE,0xFF};
 const char* path="wx360_state_test.bin";std::string error;
 check(state::StateFile::save(path,source,&error),"saves CPU and RAM snapshot");
 state::Snapshot restored;check(state::StateFile::load(path,restored,&error),"loads valid snapshot");
 check(restored.cpu.pc==source.cpu.pc&&restored.cpu.gpr[3]==source.cpu.gpr[3],"restores PC and GPR");
 check(restored.cpu.lr==source.cpu.lr&&restored.cpu.ctr==source.cpu.ctr&&restored.cpu.xer==source.cpu.xer,"restores Xenon special registers");
 check(restored.cpu.cr==source.cpu.cr&&restored.cpu.msr==source.cpu.msr&&restored.cpu.fpscr==source.cpu.fpscr,"restores control registers");
 check(restored.cpu.fpr[5]==source.cpu.fpr[5]&&restored.cpu.vr[127].lane==source.cpu.vr[127].lane,"restores FPU and VMX register files");
 check(restored.cpu.segment_registers[2]==source.cpu.segment_registers[2]&&restored.cpu.srr0==source.cpu.srr0&&restored.cpu.srr1==source.cpu.srr1&&restored.cpu.dar==source.cpu.dar,"restores segment and exception registers");
 check(restored.cpu.exception_pending&&restored.cpu.external_interrupt_pending&&restored.cpu.decrementer_interrupt_pending,"restores pending exception and interrupt state");
 check(restored.cpu.reservation_valid&&restored.cpu.reservation_address==source.cpu.reservation_address,"restores reservation state");
 check(restored.cpu.decrementer==source.cpu.decrementer&&restored.cpu.timebase_ticks==source.cpu.timebase_ticks&&restored.cpu.exception_vector==source.cpu.exception_vector,"restores timebase and decrementer state");
 check(restored.ram==source.ram,"restores RAM bytes");
 // Build a minimal legacy v1 file to preserve backward compatibility.
 std::vector<std::uint8_t> legacy_payload;
 auto put32=[](std::vector<std::uint8_t>& b,std::uint32_t v){for(unsigned i=0;i<4;++i)b.push_back(static_cast<std::uint8_t>(v>>(8*i)));};
 auto put64=[](std::vector<std::uint8_t>& b,std::uint64_t v){for(unsigned i=0;i<8;++i)b.push_back(static_cast<std::uint8_t>(v>>(8*i)));};
 for(unsigned i=0;i<32;++i)put64(legacy_payload,i==3?0xAABBCCDDU:0U);
 put64(legacy_payload,0x12340000U);put64(legacy_payload,0x1000U);put64(legacy_payload,7U);put64(legacy_payload,9U);
 put32(legacy_payload,0x12345678U);put64(legacy_payload,0x8000U);put32(legacy_payload,0xCAFEBABEU);
 std::uint64_t hash=1469598103934665603ULL;
 for(auto byte:legacy_payload){hash^=byte;hash*=1099511628211ULL;}
 const char* legacy_path="wx360_state_v1_legacy.bin";
 {std::ofstream out(legacy_path,std::ios::binary|std::ios::trunc);out.write("WX360ST\0",8);
  auto write32=[&](std::uint32_t v){for(unsigned i=0;i<4;++i)out.put(static_cast<char>(v>>(8*i)));};
  auto write64=[&](std::uint64_t v){for(unsigned i=0;i<8;++i)out.put(static_cast<char>(v>>(8*i)));};
  write32(1);write64(0);write64(legacy_payload.size());write64(hash);
  out.write(reinterpret_cast<const char*>(legacy_payload.data()),static_cast<std::streamsize>(legacy_payload.size()));}
 state::Snapshot legacy;check(state::StateFile::load(legacy_path,legacy,&error),"loads legacy version 1 save state");
 check(legacy.cpu.gpr[3]==0xAABBCCDDU&&legacy.cpu.pc==0x12340000U&&legacy.cpu.fpscr==0xCAFEBABEU,
       "restores legacy v1 CPU fields while defaulting new context");
 std::remove(legacy_path);
 {FILE*f=std::fopen(path,"r+b");if(f){std::fseek(f,-1,SEEK_END);int v=std::fgetc(f);std::fseek(f,-1,SEEK_END);std::fputc(v^0xFF,f);std::fclose(f);}}
 check(!state::StateFile::load(path,restored,&error),"rejects corrupted snapshot checksum");
 std::remove(path);return failures?EXIT_FAILURE:EXIT_SUCCESS;
}