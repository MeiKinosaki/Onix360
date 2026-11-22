#include "wx360/disc/iso_image.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
namespace {
int failures=0;
void check(bool ok,const char* message){if(!ok){std::cerr<<"FAIL: "<<message<<'\n';++failures;}}
void put_le16(std::array<unsigned char,wx360::disc::IsoImage::kSectorSize>& b,std::size_t p,std::uint16_t v){b[p]=static_cast<unsigned char>(v);b[p+1]=static_cast<unsigned char>(v>>8);}
void put_le32(std::array<unsigned char,wx360::disc::IsoImage::kSectorSize>& b,std::size_t p,std::uint32_t v){for(unsigned i=0;i<4;++i)b[p+i]=static_cast<unsigned char>(v>>(8*i));}
}
int main(){
 using wx360::disc::IsoImage;
 const std::string path="wx360_test_disc.iso";
 {
  std::ofstream out(path,std::ios::binary); std::array<unsigned char,IsoImage::kSectorSize> sector{};
  for(unsigned i=0;i<35;++i){
   sector.fill(0);
   if(i==IsoImage::kVolumeDescriptorSector){
    for(std::size_t j=0;j<IsoImage::kXdvdfsMagic.size();++j)sector[j]=static_cast<unsigned char>(IsoImage::kXdvdfsMagic[j]);
    put_le32(sector,20,33); put_le32(sector,24,2048);
   }else if(i==33){
    put_le16(sector,0,0xFFFF); put_le16(sector,2,0xFFFF); put_le32(sector,4,34); put_le32(sector,8,16);
    const std::string name="default.xex"; sector[13]=static_cast<unsigned char>(name.size());
    std::copy(name.begin(),name.end(),sector.begin()+14);
   }
   out.write(reinterpret_cast<const char*>(sector.data()),sector.size());
  }
 }
 IsoImage image;
 check(image.open(path),"recognizes synthetic XDVDFS descriptor");
 check(image.is_open(),"image remains open after validation");
 check(image.root_sector()==33,"reads root directory sector");
 check(image.root_size()==2048,"reads root directory size");
 wx360::disc::XdvdfsFileEntry executable;
 check(image.find_root_file("DEFAULT.XEX",executable),"finds default.xex case-insensitively");
 check(executable.first_sector==34&&executable.size==16,"reads executable extent");
 check(!executable.is_directory,"identifies executable as a file");
 std::array<std::uint8_t,IsoImage::kSectorSize> data{};
 check(image.read_sector(32,data.data(),data.size()),"reads sector on demand");
 image.close(); check(!image.is_open(),"close releases image"); std::remove(path.c_str());
 const std::string invalid="wx360_invalid_disc.iso";
 {std::ofstream out(invalid,std::ios::binary);std::array<char,34*2048> zeros{};out.write(zeros.data(),zeros.size());}
 check(!image.open(invalid),"rejects image without XDVDFS magic");
 check(!image.error().empty(),"reports validation error"); std::remove(invalid.c_str());
 return failures==0?EXIT_SUCCESS:EXIT_FAILURE;
}
