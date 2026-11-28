#include "onix360/storage/data_directories.hpp"
#include <cstdlib>
#include <filesystem>
#include <iostream>
int main(){
 int failures=0;
 auto check=[&](bool ok,const char* label){if(!ok){std::cerr<<"FAIL: "<<label<<'\\n';++failures;}};
 const std::filesystem::path root="onix360_data_directories_test";
 std::error_code ec;std::filesystem::remove_all(root,ec);
 onix360::storage::DataDirectories dirs(root);
 check(dirs.create(&ec),"creates data directories");
 check(std::filesystem::is_directory(dirs.roms()),"creates roms directory");
 check(std::filesystem::is_directory(dirs.covers()),"creates covers directory");
 check(std::filesystem::is_directory(dirs.saves()),"creates saves directory");
 check(std::filesystem::is_directory(dirs.configs()),"creates configs directory");
 check(dirs.create(&ec),"creation is idempotent");
 check(dirs.roms()==root/"roms","ROM path is stable");
 check(dirs.covers()==root/"covers","cover path is stable");
 check(dirs.saves()==root/"saves","save path is stable");
 check(dirs.configs()==root/"configs","config path is stable");
 check(onix360::storage::DataDirectories::portable_root("portable/Onix360.exe")==std::filesystem::absolute("portable/Onix360-data"),"portable data root stays beside executable");
 std::filesystem::remove_all(root,ec);
 return failures?EXIT_FAILURE:EXIT_SUCCESS;
}
