#include "wx360/gpu/xenos_framebuffer.hpp"
#include <cstdlib>
#include <iostream>
int main(){
 wx360::gpu::XenosFramebuffer fb;int failures=0;
 auto check=[&](bool ok,const char* s){if(!ok){std::cerr<<"FAIL: "<<s<<'\n';++failures;}};
 check(fb.resize(640,480),"allocates bounded software framebuffer");
 fb.clear(0xFF112233U);
 check(fb.write_pixel(3,4,0xFFAABBCCU),"writes framebuffer pixel");
 check(!fb.write_pixel(640,4,0),"rejects out-of-range pixel");
 check(fb.present(),"presents frame");
 check(fb.front_buffer().size()==640U*480U &&
       fb.front_buffer()[4U*640U+3U]==0xFFAABBCCU,"front buffer receives rendered frame");
 fb.clear(0xFF000000U);
 check(fb.draw_triangle(10.0F,10.0F,30.0F,10.0F,10.0F,30.0F,0xFFFF0000U),
       "rasterizes a bounded software triangle");
 check(fb.pixels()[15U*640U+15U]==0xFFFF0000U,
       "triangle rasterizer fills an interior pixel");
 check(fb.pixels()[29U*640U+29U]==0xFF000000U,
       "triangle rasterizer leaves pixels outside primitive unchanged");
 check(!fb.draw_triangle(1.0F,1.0F,2.0F,2.0F,3.0F,3.0F,0xFFFFFFFFU),
       "rejects degenerate triangle");
 check(!fb.resize(4096,4096),"rejects excessive framebuffer dimensions");
 return failures?EXIT_FAILURE:EXIT_SUCCESS;
}
