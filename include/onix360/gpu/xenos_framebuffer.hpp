#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <cmath>
#include <vector>
#include <span>

namespace onix360::gpu {

// Host-side software framebuffer scaffold. Its command IDs are Onix360-internal;
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
    // Minimal host-side triangle rasterization stage for framebuffer tests.
    // It is not a substitute for Xenos PM4 command or shader execution.
    [[nodiscard]] bool draw_triangle(float x0, float y0, float x1, float y1,
                                      float x2, float y2, std::uint32_t rgba) noexcept {
        if (pixels_.empty() || !std::isfinite(x0) || !std::isfinite(y0) ||
            !std::isfinite(x1) || !std::isfinite(y1) ||
            !std::isfinite(x2) || !std::isfinite(y2) ||
            std::fabs(x0) > 1000000.0F || std::fabs(y0) > 1000000.0F ||
            std::fabs(x1) > 1000000.0F || std::fabs(y1) > 1000000.0F ||
            std::fabs(x2) > 1000000.0F || std::fabs(y2) > 1000000.0F) return false;
        const float area = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0);
        if (area == 0.0F) return false;
        const int min_x = (std::max)(0, static_cast<int>(std::floor((std::min)({x0,x1,x2}))));
        const int min_y = (std::max)(0, static_cast<int>(std::floor((std::min)({y0,y1,y2}))));
        const int max_x = (std::min)(static_cast<int>(width_) - 1,
                                   static_cast<int>(std::ceil((std::max)({x0,x1,x2}))));
        const int max_y = (std::min)(static_cast<int>(height_) - 1,
                                   static_cast<int>(std::ceil((std::max)({y0,y1,y2}))));
        if (min_x > max_x || min_y > max_y) return true;
        const float inverse_area = 1.0F / area;
        for (int y = min_y; y <= max_y; ++y) {
            for (int x = min_x; x <= max_x; ++x) {
                const float px = static_cast<float>(x) + 0.5F;
                const float py = static_cast<float>(y) + 0.5F;
                const float w0 = (x1 - px) * (y2 - py) - (y1 - py) * (x2 - px);
                const float w1 = (x2 - px) * (y0 - py) - (y2 - py) * (x0 - px);
                const float w2 = (x0 - px) * (y1 - py) - (y0 - py) * (x1 - px);
                if ((area > 0.0F && w0 >= 0.0F && w1 >= 0.0F && w2 >= 0.0F) ||
                    (area < 0.0F && w0 <= 0.0F && w1 <= 0.0F && w2 <= 0.0F))
                    pixels_[static_cast<std::size_t>(y) * width_ +
                            static_cast<std::size_t>(x)] = rgba;
            }
        }
        return true;
    }
    // Bounded linear RGBA8 texture copy with nearest-neighbour scaling.
    // This is a software texture stage, not Xenos tiled/swizzled memory decoding.
    [[nodiscard]] bool blit_rgba8(std::span<const std::uint32_t> source,
        std::uint32_t source_width, std::uint32_t source_height,
        std::uint32_t dst_x, std::uint32_t dst_y,
        std::uint32_t dst_width, std::uint32_t dst_height) noexcept {
        if (source_width == 0 || source_height == 0 || dst_width == 0 ||
            dst_height == 0 || pixels_.empty()) return false;
        const std::uint64_t source_count =
            static_cast<std::uint64_t>(source_width) * source_height;
        if (source_count != source.size() ||
            dst_x >= width_ || dst_y >= height_) return false;
        const auto copy_width = (std::min)(dst_width, width_ - dst_x);
        const auto copy_height = (std::min)(dst_height, height_ - dst_y);
        for (std::uint32_t y = 0; y < copy_height; ++y) {
            const auto sy = static_cast<std::uint32_t>(
                (static_cast<std::uint64_t>(y) * source_height) / dst_height);
            for (std::uint32_t x = 0; x < copy_width; ++x) {
                const auto sx = static_cast<std::uint32_t>(
                    (static_cast<std::uint64_t>(x) * source_width) / dst_width);
                pixels_[static_cast<std::size_t>(dst_y + y) * width_ + dst_x + x] =
                    source[static_cast<std::size_t>(sy) * source_width + sx];
            }
        }
        return true;
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
} // namespace onix360::gpu
