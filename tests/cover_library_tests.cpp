#include "wx360/library/cover_library.hpp"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
namespace {
int failures=0;
void check(bool ok,const char* message){if(!ok){std::cerr<<"FAIL: "<<message<<'\n';++failures;}}
bool fake_download(std::string_view url,const std::filesystem::path& path,void* context){
 auto* calls=static_cast<unsigned*>(context);++*calls;
 if(url!="https://covers.example/game.jpg")return false;
 std::ofstream out(path,std::ios::binary);const unsigned char jpeg[]={0xFF,0xD8,0xFF,0xD9};out.write(reinterpret_cast<const char*>(jpeg),sizeof(jpeg));return static_cast<bool>(out);
}
bool failed_download(std::string_view,const std::filesystem::path&,void*){return false;}
}
int main(){
 using namespace wx360::covers;
 const std::filesystem::path dir="wx360_cover_test";std::error_code ec;std::filesystem::remove_all(dir,ec);
 CoverLibrary library(dir);CoverRecord game{"GAME-123","Example Game","https://covers.example/game.jpg"};
 unsigned calls=0;
 check(!library.has_cover(game.game_id),"cover cache starts empty");
 check(library.ensure_cover(game,fake_download,&calls),"downloads missing cover");
 check(library.has_cover(game.game_id),"stores downloaded cover in cache");
 check(calls==1,"downloads only once");
 check(library.ensure_cover(game,fake_download,&calls)&&calls==1,"reuses cached cover");
 check(library.cached_covers().size()==1,"enumerates cached covers");
 CoverRecord invalid{"../escape","Invalid","https://covers.example/game.jpg"};
 check(!library.ensure_cover(invalid,fake_download,&calls),"rejects unsafe game identifier");
 CoverRecord broken{"GAME-FAIL","Broken","https://covers.example/fail.jpg"};
 check(!library.ensure_cover(broken,failed_download,nullptr),"does not publish failed download");
 check(!library.has_cover(broken.game_id),"failed download leaves no cached cover");
 std::filesystem::remove_all(dir,ec);return failures?EXIT_FAILURE:EXIT_SUCCESS;
}