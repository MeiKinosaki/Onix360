#include "wx360/state/state_file.hpp"
#include <cstdio>
#include <cstdlib>
#include <iostream>
namespace { int failures=0; void check(bool ok,const char*m){if(!ok){std::cerr<<"FAIL: "<<m<<'\n';++failures;}} }
int main(){
 using namespace wx360;
 state::Snapshot source; source.cpu.pc=0x123456789ABCDEF0ULL;source.cpu.gpr[3]=0xDEADBEEF12345678ULL;
 source.cpu.lr=0x1000;source.cpu.ctr=77;source.cpu.xer=0x20000000;source.cpu.cr=0xA1234567;
 source.cpu.msr=0x8000000000000000ULL;source.cpu.fpscr=0xCAFEBABE;
 source.ram={0,1,2,3,0xFE,0xFF};
 const char* path="wx360_state_test.bin";std::string error;
 check(state::StateFile::save(path,source,&error),"saves CPU and RAM snapshot");
 state::Snapshot restored;check(state::StateFile::load(path,restored,&error),"loads valid snapshot");
 check(restored.cpu.pc==source.cpu.pc&&restored.cpu.gpr[3]==source.cpu.gpr[3],"restores PC and GPR");
 check(restored.cpu.lr==source.cpu.lr&&restored.cpu.ctr==source.cpu.ctr&&restored.cpu.xer==source.cpu.xer,"restores Xenon special registers");
 check(restored.cpu.cr==source.cpu.cr&&restored.cpu.msr==source.cpu.msr&&restored.cpu.fpscr==source.cpu.fpscr,"restores control registers");
 check(restored.ram==source.ram,"restores RAM bytes");
 {FILE*f=std::fopen(path,"r+b");if(f){std::fseek(f,-1,SEEK_END);int v=std::fgetc(f);std::fseek(f,-1,SEEK_END);std::fputc(v^0xFF,f);std::fclose(f);}}
 check(!state::StateFile::load(path,restored,&error),"rejects corrupted snapshot checksum");
 std::remove(path);return failures?EXIT_FAILURE:EXIT_SUCCESS;
}