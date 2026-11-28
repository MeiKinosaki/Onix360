#include "wx360/gpu/renderer_backend.hpp"
#include <cstdlib>
#include <iostream>
using namespace wx360::gpu;
int main() {
    int failures = 0;
    auto check = [&](bool ok, const char* label) {
        if (!ok) { std::cerr << "FAIL: " << label << '\n'; ++failures; }
    };
    const RendererAvailability software_only{};
    auto selected = select_renderer(RendererBackend::Automatic, software_only);
    check(selected.available && selected.selected == RendererBackend::Software &&
          !selected.fallback, "automatic selection uses software when no native backend is built");
    selected = select_renderer(RendererBackend::Vulkan, software_only);
    check(selected.available && selected.selected == RendererBackend::Software &&
          selected.fallback, "unavailable Vulkan request falls back to software");
    selected = select_renderer(RendererBackend::Direct3D11, software_only);
    check(selected.selected == RendererBackend::Software && selected.fallback,
          "unavailable Direct3D 11 falls back to software");
    selected = select_renderer(RendererBackend::Direct3D12, software_only);
    check(selected.selected == RendererBackend::Software && selected.fallback,
          "unavailable Direct3D 12 falls back to software");
    selected = select_renderer(RendererBackend::OpenGL, software_only);
    check(selected.selected == RendererBackend::Software && selected.fallback,
          "unavailable OpenGL falls back to software");
    const RendererAvailability all_native{true,true,true,true,true};
    selected = select_renderer(RendererBackend::Automatic, all_native);
    check(selected.selected == RendererBackend::Vulkan && selected.available,
          "automatic policy prefers Vulkan when explicitly available");
    selected = select_renderer(RendererBackend::Direct3D12, all_native);
    check(selected.selected == RendererBackend::Direct3D12 && !selected.fallback,
          "explicit available backend is selected");
    check(renderer_name(RendererBackend::Software) == "Software Renderer",
          "backend exposes readable software renderer name");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
