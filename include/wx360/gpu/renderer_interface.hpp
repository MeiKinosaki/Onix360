#pragma once
#include "wx360/gpu/renderer_backend.hpp"
#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

namespace wx360::gpu {

struct RendererFrameView {
    std::uint32_t width{};
    std::uint32_t height{};
    std::span<const std::uint32_t> rgba8{};
};

class IRendererBackend {
public:
    virtual ~IRendererBackend() = default;
    [[nodiscard]] virtual RendererBackend backend() const noexcept = 0;
    [[nodiscard]] virtual bool initialize(std::uint32_t width,
                                          std::uint32_t height) = 0;
    [[nodiscard]] virtual bool present(RendererFrameView frame) = 0;
};

// Host-memory presentation target for the software path. Rasterization is
// performed by XenosFramebuffer; this adapter owns a bounded presented copy.
class SoftwareRendererBackend final : public IRendererBackend {
public:
    [[nodiscard]] RendererBackend backend() const noexcept override {
        return RendererBackend::Software;
    }

    [[nodiscard]] bool initialize(std::uint32_t width,
                                  std::uint32_t height) override {
        if (!valid_dimensions(width, height)) return false;
        try {
            front_buffer_.assign(static_cast<std::size_t>(width) * height, 0xFF000000U);
        } catch (...) {
            return false;
        }
        width_ = width;
        height_ = height;
        return true;
    }

    [[nodiscard]] bool present(RendererFrameView frame) override {
        if (frame.width != width_ || frame.height != height_ ||
            frame.rgba8.size() != static_cast<std::size_t>(width_) * height_ ||
            front_buffer_.empty()) return false;
        std::copy(frame.rgba8.begin(), frame.rgba8.end(), front_buffer_.begin());
        ++present_count_;
        return true;
    }

    [[nodiscard]] std::span<const std::uint32_t> front_buffer() const noexcept {
        return front_buffer_;
    }
    [[nodiscard]] std::uint64_t present_count() const noexcept {
        return present_count_;
    }

private:
    [[nodiscard]] static bool valid_dimensions(std::uint32_t width,
                                              std::uint32_t height) noexcept {
        return width != 0 && height != 0 && width <= 1920 && height <= 1080;
    }
    std::uint32_t width_{};
    std::uint32_t height_{};
    std::vector<std::uint32_t> front_buffer_{};
    std::uint64_t present_count_{};
};
} // namespace wx360::gpu
