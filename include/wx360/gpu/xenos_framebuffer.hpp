#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace wx360::gpu {

// Host-side software framebuffer scaffold. Its command IDs are WX360-internal;
// they are not claimed to decode native Xenos PM4 packets.
class XenosFramebuffer {
public:
    [[nodiscard]] bool resize(std::uint32_t width,std::uint32_t height) {
        if(width==0||height==0||width>1920||height>1080) return false;
        const std::uint64_t count=static_cast<std::uint64_t>(width)*height;
        if(count>std::numeric_limits<std::size_t>::max()/sizeof(std::uint32_t)) return false;
        width_=width;height_=height;
        pixels_.assign(static_cast<std::size_t>(count),0xFF000000U);
        ++generation_;
        return true;
    }
    void clear(std::uint32_t rgba) noexcept { std::fill(pixels_.begin(),pixels_.end(),rgba); }
    [[nodiscard]] bool write_pixel(std::uint32_t x,std::uint32_t y,std::uint32_t rgba) noexcept {
        if(x>=width_||y>=height_) return false;
        pixels_[static_cast<std::size_t>(y)*width_+x]=rgba;return true;
    }
    [[nodiscard]] bool present() {
        if(pixels_.empty()) return false;
        front_buffer_=pixels_;++present_count_;return true;
    }
    [[nodiscard]] const std::vector<std::uint32_t>& pixels() const noexcept{return pixels_;}
    [[nodiscard]] const std::vector<std::uint32_t>& front_buffer() const noexcept{return front_buffer_;}
    [[nodiscard]] std::uint32_t width()const noexcept{return width_;}
    [[nodiscard]] std::uint32_t height()const noexcept{return height_;}
    [[nodiscard]] std::uint64_t present_count()const noexcept{return present_count_;}
    [[nodiscard]] std::uint64_t generation()const noexcept{return generation_;}
private:
    std::uint32_t width_{},height_{};
    std::vector<std::uint32_t> pixels_,front_buffer_;
    std::uint64_t present_count_{},generation_{};
};
} // namespace wx360::gpu
