#pragma once
#include "wx360/gpu/renderer_interface.hpp"
#ifdef _WIN32
#include "wx360/gpu/renderer_d3d11.hpp"
#endif
#include <memory>
#include <utility>

namespace wx360::gpu {

struct RendererInstance {
    std::unique_ptr<IRendererBackend> implementation{};
    RendererBackend selected{RendererBackend::Software};
    bool fallback{};
    [[nodiscard]] explicit operator bool() const noexcept {
        return static_cast<bool>(implementation);
    }
};

[[nodiscard]] inline RendererInstance create_renderer(
    RendererBackend requested, std::uint32_t width, std::uint32_t height) {
    const auto selection = select_renderer(
        requested, compiled_renderer_availability());
#ifdef _WIN32
    if (selection.selected == RendererBackend::Direct3D11) {
        auto native = std::make_unique<Direct3D11RendererBackend>();
        if (native->initialize(width, height))
            return {std::move(native), RendererBackend::Direct3D11,
                    selection.fallback};
    }
#endif
    auto software = std::make_unique<SoftwareRendererBackend>();
    if (!software->initialize(width, height)) return {};
    return {std::move(software), RendererBackend::Software,
            selection.selected != RendererBackend::Software || selection.fallback};
}

} // namespace wx360::gpu
