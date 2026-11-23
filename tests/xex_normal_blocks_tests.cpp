#include "wx360/xex/xex_normal_blocks.hpp"
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

static void be32(std::vector<std::uint8_t>& b,std::size_t p,std::uint32_t v){
 b[p]=v>>24;b[p+1]=v>>16;b[p+2]=v>>8;b[p+3]=v;
}
int main(){
 using namespace wx360::xex;
 // One authenticated XEX Normal block: embedded zero descriptor, one chunk,
 // and a zero-length chunk terminator.
 std::vector<std::uint8_t> stream(24+2+5+2,0);
 stream[24]=0;stream[25]=5;
 stream[26]='h';stream[27]='e';stream[28]='l';stream[29]='l';stream[30]='o';
 stream[31]=0;stream[32]=0;
 NormalBlockDescriptor first{};
 first.block_size=static_cast<std::uint32_t>(stream.size());
 first.hash=wx360::crypto::sha1(stream);
 std::vector<std::uint8_t> output;
 if(unpack_normal_chunks(stream,first,5,output)!=NormalBlockError::Ok ||
    output.size()!=5 || std::string(output.begin(),output.end())!="hello"){
   std::cerr<<"XEX Normal block/chunk test failed\\n";return EXIT_FAILURE;
 }
 stream[27]^=1;
 if(unpack_normal_chunks(stream,first,5,output)!=NormalBlockError::HashMismatch){
   std::cerr<<"Corrupted Normal block was not rejected\\n";return EXIT_FAILURE;
 }
 return EXIT_SUCCESS;
}
