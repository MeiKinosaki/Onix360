#pragma once
#include <array>
#include <cstdlib>
#include <filesystem>
#include <system_error>
#include <utility>

namespace wx360::storage {
class DataDirectories {
public:
 explicit DataDirectories(std::filesystem::path root):root_(std::move(root)){}
 const std::filesystem::path& root() const noexcept{return root_;}
 std::filesystem::path roms() const{return root_/"roms";}
 std::filesystem::path covers() const{return root_/"covers";}
 std::filesystem::path saves() const{return root_/"saves";}
 bool create(std::error_code* error=nullptr) const {
  std::error_code ec;
  for(const auto& path:std::array<std::filesystem::path,4>{root_,roms(),covers(),saves()}){
   std::filesystem::create_directories(path,ec);
   if(ec){if(error)*error=ec;return false;}
  }
  if(error)error->clear();
  return true;
 }
 static std::filesystem::path default_root(){
#if defined(_WIN32)
  if(const char* base=std::getenv("LOCALAPPDATA");base&&*base)
   return std::filesystem::path(base)/"WX360";
#endif
  if(const char* home=std::getenv("HOME");home&&*home)
   return std::filesystem::path(home)/".local"/"share"/"WX360";
  return std::filesystem::current_path()/"WX360-data";
 }
private:
 std::filesystem::path root_;
};
}
