#pragma once

#include <d3d12.h>
#include <dxgiformat.h>

#include <cstdint>
#include <memory>

namespace DeepRun::Diagnostics
{
class Logger;
}

namespace DeepRun::Render
{
class D3D12Renderer final
{
public:
    explicit D3D12Renderer(Diagnostics::Logger& logger);
    ~D3D12Renderer();

    D3D12Renderer(const D3D12Renderer&) = delete;
    D3D12Renderer& operator=(const D3D12Renderer&) = delete;

    [[nodiscard]] bool Initialize(void* windowHandle, std::uint32_t width, std::uint32_t height, bool vsync);
    void Resize(std::uint32_t width, std::uint32_t height);
    void WaitForIdle();
    void BeginFrame();
    void EndFrame();

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] ID3D12Device* Device() const noexcept;
    [[nodiscard]] ID3D12GraphicsCommandList* CommandList() const noexcept;
    [[nodiscard]] ID3D12DescriptorHeap* ImGuiDescriptorHeap() const noexcept;
    [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE ImGuiCpuHandle() const noexcept;
    [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE ImGuiGpuHandle() const noexcept;
    [[nodiscard]] DXGI_FORMAT BackBufferFormat() const noexcept;
    [[nodiscard]] std::uint32_t FrameCount() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}
