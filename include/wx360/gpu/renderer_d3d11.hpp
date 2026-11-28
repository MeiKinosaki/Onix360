#pragma once
#include "wx360/gpu/renderer_interface.hpp"

#ifdef _WIN32
#include <d3d11.h>
#include <wrl/client.h>

namespace wx360::gpu {

// Native D3D11 device and GPU texture upload backend.
// The texture is an off-screen target; swap-chain presentation and Xenos shaders
// remain separate, unfinished integration work.
class Direct3D11RendererBackend final : public IRendererBackend {
public:
    [[nodiscard]] RendererBackend backend() const noexcept override {
        return RendererBackend::Direct3D11;
    }

    [[nodiscard]] bool initialize(std::uint32_t width,
                                  std::uint32_t height) override {
        if (width == 0 || height == 0 || width > 1920 || height > 1080)
            return false;
        device_.Reset();
        context_.Reset();
        texture_.Reset();
        UINT flags = 0;
#if defined(_DEBUG)
        flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
        D3D_FEATURE_LEVEL obtained{};
        HRESULT hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0,
            D3D11_SDK_VERSION, &device_, &obtained, &context_);
        if (FAILED(hr)) {
            hr = D3D11CreateDevice(
                nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, nullptr, 0,
                D3D11_SDK_VERSION, &device_, &obtained, &context_);
            if (FAILED(hr)) return false;
        }
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        if (FAILED(device_->CreateTexture2D(&desc, nullptr, &texture_))) {
            device_.Reset();
            context_.Reset();
            return false;
        }
        width_ = width;
        height_ = height;
        return true;
    }

    [[nodiscard]] bool present(RendererFrameView frame) override {
        if (!device_ || !context_ || !texture_ ||
            frame.width != width_ || frame.height != height_ ||
            frame.rgba8.size() != static_cast<std::size_t>(width_) * height_)
            return false;
        context_->UpdateSubresource(texture_.Get(), 0, nullptr,
            frame.rgba8.data(), width_ * sizeof(std::uint32_t), 0);
        ++present_count_;
        return true;
    }

    [[nodiscard]] std::uint64_t present_count() const noexcept {
        return present_count_;
    }

private:
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture_;
    std::uint32_t width_{};
    std::uint32_t height_{};
    std::uint64_t present_count_{};
};
} // namespace wx360::gpu
#endif
