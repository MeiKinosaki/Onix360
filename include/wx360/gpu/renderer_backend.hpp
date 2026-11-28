#pragma once
#include <array>
#include <cstdint>
#include <string_view>

namespace wx360::gpu {

enum class RendererBackend : std::uint8_t {
    Automatic, Vulkan, Direct3D11, Direct3D12, OpenGL, Software
};

struct RendererAvailability {
    bool vulkan{};
    bool direct3d11{};
    bool direct3d12{};
    bool opengl{};
    bool software{true};
};

struct RendererSelection {
    RendererBackend requested{RendererBackend::Automatic};
    RendererBackend selected{RendererBackend::Software};
    bool fallback{};
    bool available{};
};

[[nodiscard]] constexpr RendererAvailability compiled_renderer_availability() noexcept {
    RendererAvailability result{};
    result.software = true;
#ifdef _WIN32
    result.direct3d11 = true;
#endif
    return result;
}

[[nodiscard]] constexpr bool renderer_available(
    RendererBackend backend, const RendererAvailability& availability) noexcept {
    switch (backend) {
    case RendererBackend::Vulkan: return availability.vulkan;
    case RendererBackend::Direct3D11: return availability.direct3d11;
    case RendererBackend::Direct3D12: return availability.direct3d12;
    case RendererBackend::OpenGL: return availability.opengl;
    case RendererBackend::Software: return availability.software;
    case RendererBackend::Automatic: return true;
    }
    return false;
}

[[nodiscard]] constexpr RendererSelection select_renderer(
    RendererBackend requested, const RendererAvailability& availability) noexcept {
    if (requested == RendererBackend::Automatic) {
        constexpr std::array<RendererBackend,5> preference{
            RendererBackend::Vulkan, RendererBackend::Direct3D12,
            RendererBackend::Direct3D11, RendererBackend::OpenGL,
            RendererBackend::Software};
        for (const auto backend : preference)
            if (renderer_available(backend, availability))
                return {requested, backend, false, true};
        return {requested, RendererBackend::Software, true, false};
    }
    if (renderer_available(requested, availability))
        return {requested, requested, false, true};
    if (availability.software)
        return {requested, RendererBackend::Software, true, true};
    return {requested, RendererBackend::Software, true, false};
}

[[nodiscard]] constexpr std::string_view renderer_name(
    RendererBackend backend) noexcept {
    switch (backend) {
    case RendererBackend::Automatic: return "Automatic";
    case RendererBackend::Vulkan: return "Vulkan";
    case RendererBackend::Direct3D11: return "Direct3D 11";
    case RendererBackend::Direct3D12: return "Direct3D 12";
    case RendererBackend::OpenGL: return "OpenGL";
    case RendererBackend::Software: return "Software Renderer";
    }
    return "Unknown";
}

// This is backend-selection policy only. A backend becomes available only
// after its native device/context implementation and integration are built.
} // namespace wx360::gpu
