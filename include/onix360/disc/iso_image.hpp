#pragma once
#include <cctype>
#include <cstdint>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>
namespace onix360::disc {
struct XdvdfsFileEntry { std::string name; std::uint32_t first_sector{0}; std::uint32_t size{0}; bool is_directory{false}; };
class IsoImage {
public:
 static constexpr std::uint32_t kSectorSize=2048, kVolumeDescriptorSector=32;
 static constexpr std::string_view kXdvdfsMagic="MICROSOFT*XBOX*MEDIA";
 static constexpr std::uint8_t kDirectoryAttribute=0x10;
 bool open(const std::string& path) {
  close(); file_.open(path,std::ios::binary); if(!file_) return fail("unable to open ISO image");
  file_.seekg(0,std::ios::end); auto end=file_.tellg();
  if(end<0||static_cast<std::uint64_t>(end)<(kVolumeDescriptorSector+1ULL)*kSectorSize) return fail("image too small for XDVDFS descriptor");
  image_size_=static_cast<std::uint64_t>(end); std::uint8_t d[kSectorSize]{};
  if(!read_sector(kVolumeDescriptorSector,d,sizeof(d))) return fail("unable to read volume descriptor");
  for(std::size_t i=0;i<kXdvdfsMagic.size();++i) if(d[i]!=static_cast<std::uint8_t>(kXdvdfsMagic[i])) return fail("XDVDFS magic not found");
  root_sector_=le32(d+20); root_size_=le32(d+24);
  if(!root_size_||!range_valid(root_sector_,root_size_)) return fail("invalid XDVDFS root extent");
  error_.clear(); return true;
 }
 void close() noexcept { if(file_.is_open()) file_.close(); image_size_=0; root_sector_=root_size_=0; error_.clear(); }
 bool is_open() const noexcept { return file_.is_open()&&error_.empty(); }
 std::uint32_t root_sector() const noexcept { return root_sector_; }
 std::uint32_t root_size() const noexcept { return root_size_; }
 const std::string& error() const noexcept { return error_; }
 bool read_sector(std::uint32_t s,std::uint8_t* out,std::size_t n) {
  if(!file_.is_open()||!out||n!=kSectorSize) return false;
  std::uint64_t off=static_cast<std::uint64_t>(s)*kSectorSize;
  if(off>image_size_||n>image_size_-off) return false;
  file_.clear(); file_.seekg(static_cast<std::streamoff>(off)); file_.read(reinterpret_cast<char*>(out),static_cast<std::streamsize>(n));
  return file_.gcount()==static_cast<std::streamsize>(n);
 }
 // Reads a bounded portion of a file without loading the whole ISO or file.
 bool read_file_range(const XdvdfsFileEntry& entry,std::uint32_t offset,std::uint8_t* output,std::size_t count) {
  if(!is_open()||entry.is_directory||(!output&&count)||offset>entry.size||count>entry.size-offset) return false;
  std::uint64_t absolute=static_cast<std::uint64_t>(entry.first_sector)*kSectorSize+offset;
  if(absolute>image_size_||count>image_size_-absolute) return false;
  file_.clear(); file_.seekg(static_cast<std::streamoff>(absolute));
  file_.read(reinterpret_cast<char*>(output),static_cast<std::streamsize>(count));
  return file_.gcount()==static_cast<std::streamsize>(count);
 }
 // Traverses the XDVDFS root directory's offset-linked entry tree.
 bool find_root_file(std::string_view wanted,XdvdfsFileEntry& result) {
  if(!is_open()||wanted.empty()) return false;
  std::vector<std::uint8_t> dir(root_size_); std::uint64_t base=static_cast<std::uint64_t>(root_sector_)*kSectorSize;
  file_.clear(); file_.seekg(static_cast<std::streamoff>(base)); file_.read(reinterpret_cast<char*>(dir.data()),static_cast<std::streamsize>(dir.size()));
  if(file_.gcount()!=static_cast<std::streamsize>(dir.size())) return fail("unable to read root directory");
  std::vector<std::uint32_t> todo{0}; std::vector<bool> seen(dir.size(),false);
  while(!todo.empty()) {
   auto pos=todo.back(); todo.pop_back(); if(pos==0xFFFFU) continue;
   if(pos>=dir.size()||seen[pos]) return fail("invalid or cyclic XDVDFS directory index"); seen[pos]=true;
   if(dir.size()-pos<14) return fail("truncated XDVDFS directory entry");
   const auto* e=dir.data()+pos; auto left=le16(e),right=le16(e+2); auto sector=le32(e+4),size=le32(e+8);
   auto attr=e[12],len=e[13]; if(!len||len>dir.size()-pos-14) return fail("invalid XDVDFS filename length");
   std::string name(reinterpret_cast<const char*>(e+14),len);
   if(equal_name(name,wanted)) {
    if(!range_valid(sector,size)) return fail("XDVDFS file extent outside ISO");
    result={std::move(name),sector,size,(attr&kDirectoryAttribute)!=0}; return true;
   }
   if(right&&right!=0xFFFFU) todo.push_back(right); if(left&&left!=0xFFFFU) todo.push_back(left);
  }
  return false;
 }
private:
 static std::uint16_t le16(const std::uint8_t*p){return static_cast<std::uint16_t>(p[0]|(static_cast<std::uint16_t>(p[1])<<8));}
 static std::uint32_t le32(const std::uint8_t*p){return static_cast<std::uint32_t>(p[0])|(static_cast<std::uint32_t>(p[1])<<8)|(static_cast<std::uint32_t>(p[2])<<16)|(static_cast<std::uint32_t>(p[3])<<24);}
 static bool equal_name(std::string_view a,std::string_view b){if(a.size()!=b.size())return false;for(std::size_t i=0;i<a.size();++i)if(std::tolower(static_cast<unsigned char>(a[i]))!=std::tolower(static_cast<unsigned char>(b[i])))return false;return true;}
 bool range_valid(std::uint32_t s,std::uint32_t n)const{std::uint64_t o=static_cast<std::uint64_t>(s)*kSectorSize;return o<=image_size_&&n<=image_size_-o;}
 bool fail(const char*m){error_=m;if(file_.is_open())file_.close();return false;}
 std::ifstream file_; std::uint64_t image_size_{0}; std::uint32_t root_sector_{0},root_size_{0}; std::string error_;
};
}