#pragma once
#include <array>
#include <filesystem>
#include <system_error>
#include <utility>

namespace onix360::storage {
class DataDirectories {
public:
 explicit DataDirectories(std::filesystem::path root):root_(std::move(root)){}
 const std::filesystem::path& root() const noexcept{return root_;}
 std::filesystem::path roms() const{return root_/"roms";}
 std::filesystem::path god() const{return roms()/"GOD";}
 std::filesystem::path content() const{return roms()/"Content";}
 std::filesystem::path content_packages() const{return content()/"0000000000000000";}
 std::filesystem::path covers() const{return root_/"covers";}
 std::filesystem::path saves() const{return root_/"saves";}
 std::filesystem::path configs() const{return root_/"configs";}
 bool create(std::error_code* error=nullptr) const {
  std::error_code ec;
  for(const auto& path:std::array<std::filesystem::path,8>{root_,roms(),god(),content(),content_packages(),covers(),saves(),configs()}){
   std::filesystem::create_directories(path,ec);
   if(ec){if(error)*error=ec;return false;}
  }
  if(error)error->clear();
  return true;
 }
 // Portable mode stores writable data beside the executable.
 static std::filesystem::path portable_root(const std::filesystem::path& executable){
  std::error_code ec;
  const auto absolute=std::filesystem::absolute(executable,ec);
  const auto resolved=ec?executable:absolute;
  return resolved.parent_path()/"Onix360-data";
 }
 static std::filesystem::path default_root(){
  return portable_root(std::filesystem::current_path()/"Onix360.exe");
 }
private:
 std::filesystem::path root_;
};
}
