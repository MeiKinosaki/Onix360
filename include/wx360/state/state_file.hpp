#pragma once
#include "wx360/cpu/xenon_state.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <string>
#include <vector>
#include <utility>

namespace wx360::state {
struct Snapshot {
 cpu::XenonThreadState cpu;
 std::vector<std::uint8_t> ram;
};
class StateFile {
public:
 static constexpr std::uint32_t kVersion=2;
 static constexpr std::uint64_t kMaxRamBytes=512ULL*1024ULL*1024ULL;
 static bool save(const std::string& path,const Snapshot& snapshot,std::string* error=nullptr) {
  if(snapshot.ram.size()>kMaxRamBytes)return fail(error,"RAM snapshot exceeds safety limit");
  std::ofstream out(path,std::ios::binary|std::ios::trunc);
  if(!out)return fail(error,"cannot create save-state file");
  std::vector<std::uint8_t> payload;
  payload.reserve(kCpuV2Bytes+snapshot.ram.size());
  for(auto v:snapshot.cpu.gpr)put64(payload,v);
  for(auto v:snapshot.cpu.fpr)put64(payload,v);
  for(const auto& reg:snapshot.cpu.vr)for(auto lane:reg.lane)put32(payload,lane);
  put64(payload,snapshot.cpu.pc);put64(payload,snapshot.cpu.lr);
  put64(payload,snapshot.cpu.ctr);put64(payload,snapshot.cpu.xer);
  put64(payload,snapshot.cpu.msr);put64(payload,snapshot.cpu.srr0);
  put64(payload,snapshot.cpu.srr1);put64(payload,snapshot.cpu.dar);
  put64(payload,snapshot.cpu.reservation_address);
  put64(payload,snapshot.cpu.timebase_ticks);
  put32(payload,snapshot.cpu.cr);put32(payload,snapshot.cpu.fpscr);
  for(auto v:snapshot.cpu.segment_registers)put32(payload,v);
  put32(payload,snapshot.cpu.decrementer);put32(payload,snapshot.cpu.exception_vector);
  put8(payload,snapshot.cpu.exception_pending);
  put8(payload,snapshot.cpu.external_interrupt_pending);
  put8(payload,snapshot.cpu.decrementer_interrupt_pending);
  put8(payload,snapshot.cpu.reservation_valid);
  payload.insert(payload.end(),snapshot.ram.begin(),snapshot.ram.end());
  out.write("WX360ST\0",8); write32(out,kVersion);write64(out,snapshot.ram.size());write64(out,payload.size());
  write64(out,checksum(payload));
  out.write(reinterpret_cast<const char*>(payload.data()),static_cast<std::streamsize>(payload.size()));
  if(!out)return fail(error,"failed while writing save-state data");
  if(error)error->clear();return true;
 }
 static bool load(const std::string& path,Snapshot& destination,std::string* error=nullptr) {
  std::ifstream in(path,std::ios::binary);if(!in)return fail(error,"cannot open save-state file");
  char magic[8]{};in.read(magic,8);if(!in||std::string(magic,8)!=std::string("WX360ST\0",8))return fail(error,"invalid save-state signature");
  std::uint32_t version=0;std::uint64_t ram_size=0,payload_size=0,expected=0;
  if(!read32(in,version)||!read64(in,ram_size)||!read64(in,payload_size)||!read64(in,expected))return fail(error,"truncated save-state header");
  if(version!=1U&&version!=kVersion)return fail(error,"unsupported save-state version");
  const std::uint64_t cpu_bytes=version==1U?kCpuV1Bytes:kCpuV2Bytes;
  if(ram_size>kMaxRamBytes||payload_size!=cpu_bytes+ram_size)return fail(error,"invalid save-state payload size");
  std::vector<std::uint8_t> payload(static_cast<std::size_t>(payload_size));
  in.read(reinterpret_cast<char*>(payload.data()),static_cast<std::streamsize>(payload.size()));
  if(in.gcount()!=static_cast<std::streamsize>(payload.size()))return fail(error,"truncated save-state payload");
  char extra;if(in.get(extra))return fail(error,"unexpected trailing data in save-state");
  if(checksum(payload)!=expected)return fail(error,"save-state checksum mismatch");
  Snapshot candidate;std::size_t p=0;
  for(auto& v:candidate.cpu.gpr)v=get64(payload,p);
  if(version==1U) {
   candidate.cpu.pc=get64(payload,p);candidate.cpu.lr=get64(payload,p);
   candidate.cpu.ctr=get64(payload,p);candidate.cpu.xer=get64(payload,p);
   candidate.cpu.cr=get32(payload,p);candidate.cpu.msr=get64(payload,p);
   candidate.cpu.fpscr=get32(payload,p);
  } else {
   for(auto& v:candidate.cpu.fpr)v=get64(payload,p);
   for(auto& reg:candidate.cpu.vr)for(auto& lane:reg.lane)lane=get32(payload,p);
   candidate.cpu.pc=get64(payload,p);candidate.cpu.lr=get64(payload,p);
   candidate.cpu.ctr=get64(payload,p);candidate.cpu.xer=get64(payload,p);
   candidate.cpu.msr=get64(payload,p);candidate.cpu.srr0=get64(payload,p);
   candidate.cpu.srr1=get64(payload,p);candidate.cpu.dar=get64(payload,p);
   candidate.cpu.reservation_address=get64(payload,p);
   candidate.cpu.timebase_ticks=get64(payload,p);
   candidate.cpu.cr=get32(payload,p);candidate.cpu.fpscr=get32(payload,p);
   for(auto& v:candidate.cpu.segment_registers)v=get32(payload,p);
   candidate.cpu.decrementer=get32(payload,p);
   candidate.cpu.exception_vector=get32(payload,p);
   candidate.cpu.exception_pending=get8(payload,p)!=0;
   candidate.cpu.external_interrupt_pending=get8(payload,p)!=0;
   candidate.cpu.decrementer_interrupt_pending=get8(payload,p)!=0;
   candidate.cpu.reservation_valid=get8(payload,p)!=0;
  }
  candidate.ram.assign(payload.begin()+static_cast<std::ptrdiff_t>(p),payload.end());
  destination=std::move(candidate);if(error)error->clear();return true;
 }
private:
 static constexpr std::uint64_t kCpuV1Bytes=32ULL*8+5ULL*8+4+4;
 static constexpr std::uint64_t kCpuV2Bytes=32ULL*8+32ULL*8+128ULL*16+
     10ULL*8+4+4+16ULL*4+4+4+4;
 static bool fail(std::string* e,const char* m){if(e)*e=m;return false;}
 static void put8(std::vector<std::uint8_t>& b,std::uint8_t v){b.push_back(v?1U:0U);}
 static void put32(std::vector<std::uint8_t>& b,std::uint32_t v){for(unsigned i=0;i<4;++i)b.push_back(static_cast<std::uint8_t>(v>>(8*i)));}
 static void put64(std::vector<std::uint8_t>& b,std::uint64_t v){for(unsigned i=0;i<8;++i)b.push_back(static_cast<std::uint8_t>(v>>(8*i)));}
 static std::uint8_t get8(const std::vector<std::uint8_t>&b,std::size_t&p){return b[p++];}
 static std::uint32_t get32(const std::vector<std::uint8_t>&b,std::size_t&p){std::uint32_t v=0;for(unsigned i=0;i<4;++i)v|=static_cast<std::uint32_t>(b[p++])<<(8*i);return v;}
 static std::uint64_t get64(const std::vector<std::uint8_t>&b,std::size_t&p){std::uint64_t v=0;for(unsigned i=0;i<8;++i)v|=static_cast<std::uint64_t>(b[p++])<<(8*i);return v;}
 static std::uint64_t checksum(const std::vector<std::uint8_t>&b){std::uint64_t h=1469598103934665603ULL;for(auto v:b){h^=v;h*=1099511628211ULL;}return h;}
 static void write32(std::ofstream&o,std::uint32_t v){for(unsigned i=0;i<4;++i)o.put(static_cast<char>(v>>(8*i)));}
 static void write64(std::ofstream&o,std::uint64_t v){for(unsigned i=0;i<8;++i)o.put(static_cast<char>(v>>(8*i)));}
 static bool read32(std::ifstream&i,std::uint32_t&v){v=0;for(unsigned n=0;n<4;++n){int c=i.get();if(c<0)return false;v|=static_cast<std::uint32_t>(c)<<(8*n);}return true;}
 static bool read64(std::ifstream&i,std::uint64_t&v){v=0;for(unsigned n=0;n<8;++n){int c=i.get();if(c<0)return false;v|=static_cast<std::uint64_t>(c)<<(8*n);}return true;}
};
}
