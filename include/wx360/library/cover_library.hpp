#pragma once
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>
namespace wx360::covers {
struct CoverRecord { std::string game_id; std::string title; std::string image_url; };
class CoverLibrary {
public:
 using Download = bool(*)(std::string_view url,const std::filesystem::path& destination,void* context);
 explicit CoverLibrary(std::filesystem::path directory):directory_(std::move(directory)){}
 static bool valid_game_id(std::string_view id) noexcept {
  if(id.empty()||id.size()>64)return false;
  for(unsigned char c:id)if(!std::isalnum(c)&&c!='-'&&c!='_')return false;
  return true;
 }
 std::filesystem::path cover_path(std::string_view id)const {
  if(!valid_game_id(id))return {};
  return directory_/(std::string(id)+".jpg");
 }
 bool has_cover(std::string_view id)const {
  auto p=cover_path(id);std::error_code ec;
  return !p.empty()&&std::filesystem::is_regular_file(p,ec)&&!ec&&std::filesystem::file_size(p,ec)>0&&!ec;
 }
 // Returns a cached image first; otherwise downloads once through the configured transport.
 bool ensure_cover(const CoverRecord& game,Download download,void* context=nullptr)const {
  if(!valid_game_id(game.game_id)||game.image_url.empty()||!download)return false;
  if(has_cover(game.game_id))return true;
  std::error_code ec;std::filesystem::create_directories(directory_,ec);if(ec)return false;
  auto target=cover_path(game.game_id),temporary=target;temporary += ".part";
  std::filesystem::remove(temporary,ec);ec.clear();
  if(!download(game.image_url,temporary,context)){std::filesystem::remove(temporary,ec);return false;}
  auto size=std::filesystem::file_size(temporary,ec);
  if(ec||size<4||size>8ULL*1024ULL*1024ULL){std::filesystem::remove(temporary,ec);return false;}
  std::ifstream image(temporary,std::ios::binary);
  unsigned char signature[2]{},tail[2]{};image.read(reinterpret_cast<char*>(signature),2);
  image.seekg(-2,std::ios::end);image.read(reinterpret_cast<char*>(tail),2);
  const bool valid=image&&signature[0]==0xFF&&signature[1]==0xD8&&tail[0]==0xFF&&tail[1]==0xD9;
  image.close();
  if(!valid){std::filesystem::remove(temporary,ec);return false;}
  std::filesystem::rename(temporary,target,ec);
  if(ec){std::filesystem::remove(temporary,ec);return false;}
  return true;
 }
 std::vector<std::filesystem::path> cached_covers()const {
  std::vector<std::filesystem::path> result;std::error_code ec;
  if(!std::filesystem::is_directory(directory_,ec)||ec)return result;
  for(std::filesystem::directory_iterator it(directory_,ec),end;!ec&&it!=end;it.increment(ec)){
   if(it->is_regular_file(ec)&&!ec&&it->path().extension()==".jpg")result.push_back(it->path());
  }
  return result;
 }
private:
 std::filesystem::path directory_;
};
}