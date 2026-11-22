#pragma once
#include "wx360/cpu/xenon_state.hpp"
#include <array>
#include <cstdint>
#include <fstream>
#include <limits>
#include <string>
#include <vector>
namespace wx360::state {
struct Snapshot {
 cpu::XenonThreadState cpu;
 std::vector<std::uint8_t> ram;
};
class StateFile {
public:
 static constexpr std::uint32_t kVersion=1;
 static constexpr std::uint64_t kMaxRamBytes=512ULL*1024ULL*1024ULL;
 static bool save(const std::string& path,const Snapshot& snapshot,std::string* error=nullptr) {
  if(snapshot.ram.size()>kMaxRamBytes)return fail(error,"RAM snapshot exceeds safety limit");
  std::ofstream out(path,std::ios::binary|std::ios::trunc);
  if(!out)return fail(error,"cannot create save-state file");
  std::vector<std::uint8_t> payload;
  payload.reserve(32*8+8*8+8*4+snapshot.ram.size());
  for(auto v:snapshot.cpu.gpr)put64(payload,v);
  put64(payload,snapshot.cpu.pc);put64(payload,snapshot.cpu.lr);put64(payload,snapshot.cpu.ctr);put64(payload,snapshot.cpu.xer);
  put32(payload,snapshot.cpu.cr);put64(payload,snapshot.cpu.msr);put32(payload,snapshot.cpu.fpscr);
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
  constexpr std::uint64_t cpu_bytes=32ULL*8+5ULL*8+4+4; // GPRs, PC/LR/CTR/XER/MSR, CR and FPSCR
  if(version!=kVersion)return fail(error,"unsupported save-state version");
  if(ram_size>kMaxRamBytes||payload_size!=cpu_bytes+ram_size)return fail(error,"invalid save-state payload size");
  std::vector<std::uint8_t> payload(static_cast<std::size_t>(payload_size));
  in.read(reinterpret_cast<char*>(payload.data()),static_cast<std::streamsize>(payload.size()));
  if(in.gcount()!=static_cast<std::streamsize>(payload.size()))return fail(error,"truncated save-state payload");
  char extra;if(in.get(extra))return fail(error,"unexpected trailing data in save-state");
  if(checksum(payload)!=expected)return fail(error,"save-state checksum mismatch");
  Snapshot candidate;std::size_t p=0;
  for(auto& v:candidate.cpu.gpr)v=get64(payload,p);
  candidate.cpu.pc=get64(payload,p);candidate.cpu.lr=get64(payload,p);candidate.cpu.ctr=get64(payload,p);candidate.cpu.xer=get64(payload,p);
  candidate.cpu.cr=get32(payload,p);candidate.cpu.msr=get64(payload,p);candidate.cpu.fpscr=get32(payload,p);
  candidate.ram.assign(payload.begin()+static_cast<std::ptrdiff_t>(p),payload.end());
  destination=std::move(candidate);if(error)error->clear();return true;
 }
private:
 static bool fail(std::string* e,const char* m){if(e)*e=m;return false;}
 static void put32(std::vector<std::uint8_t>& b,std::uint32_t v){for(unsigned i=0;i<4;++i)b.push_back(static_cast<std::uint8_t>(v>>(8*i)));}
 static void put64(std::vector<std::uint8_t>& b,std::uint64_t v){for(unsigned i=0;i<8;++i)b.push_back(static_cast<std::uint8_t>(v>>(8*i)));}
 static std::uint32_t get32(const std::vector<std::uint8_t>&b,std::size_t&p){std::uint32_t v=0;for(unsigned i=0;i<4;++i)v|=static_cast<std::uint32_t>(b[p++])<<(8*i);return v;}
 static std::uint64_t get64(const std::vector<std::uint8_t>&b,std::size_t&p){std::uint64_t v=0;for(unsigned i=0;i<8;++i)v|=static_cast<std::uint64_t>(b[p++])<<(8*i);return v;}
 static std::uint64_t checksum(const std::vector<std::uint8_t>&b){std::uint64_t h=1469598103934665603ULL;for(auto v:b){h^=v;h*=1099511628211ULL;}return h;}
 static void write32(std::ofstream&o,std::uint32_t v){for(unsigned i=0;i<4;++i)o.put(static_cast<char>(v>>(8*i)));}
 static void write64(std::ofstream&o,std::uint64_t v){for(unsigned i=0;i<8;++i)o.put(static_cast<char>(v>>(8*i)));}
 static bool read32(std::ifstream&i,std::uint32_t&v){v=0;for(unsigned n=0;n<4;++n){int c=i.get();if(c<0)return false;v|=static_cast<std::uint32_t>(c)<<(8*n);}return true;}
 static bool read64(std::ifstream&i,std::uint64_t&v){v=0;for(unsigned n=0;n<8;++n){int c=i.get();if(c<0)return false;v|=static_cast<std::uint64_t>(c)<<(8*n);}return true;}
};
}