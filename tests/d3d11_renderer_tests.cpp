#include "wx360/gpu/renderer_d3d11.hpp"
#include <array>
#include <cstdlib>
#include <iostream>

int main() {
#ifdef _WIN32
    wx360::gpu::Direct3D11RendererBackend renderer;
    if (!renderer.initialize(16, 16)) {
        std::cerr << "D3D11 device/texture initialization failed\n";
        return EXIT_FAILURE;
    }
    std::array<std::uint32_t, 256> pixels{};
    pixels.fill(0xFF336699U);
    if (!renderer.present({16, 16, pixels}) || renderer.present_count() != 1) {
        std::cerr << "D3D11 frame upload failed\n";
        return EXIT_FAILURE;
    }
#endif
    return EXIT_SUCCESS;
}
