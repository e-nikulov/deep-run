#pragma once

#include "Engine/Render/Camera.h"
#include "Engine/Render/ClearRect.h"
#include "Engine/Render/DisplayOutput.h"
#include "Engine/Render/IndexedGeometry.h"
#include "Engine/Render/ModelDraw.h"

#include <d3d12.h>
#include <dxgiformat.h>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <string>

namespace DeepRun::Assets
{
struct ModelAsset;
}

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

    [[nodiscard]] bool Initialize(
        void* windowHandle,
        std::uint32_t width,
        std::uint32_t height,
        bool vsync,
        bool hdrRequested,
        const std::filesystem::path& shaderRoot);
    void Resize(std::uint32_t width, std::uint32_t height);
    void WaitForIdle();
    [[nodiscard]] std::expected<GpuModelUploadResult, std::string> UploadModel(
        const Assets::ModelAsset& model);
    [[nodiscard]] bool IsGpuModelValid(GpuModelHandle handle) const noexcept;
    [[nodiscard]] std::expected<ModelDrawStats, std::string> DrawModel(
        GpuModelHandle handle,
        std::span<const ModelDrawInstance> draws,
        const OrthographicCamera& camera);

    // Clears a rectangular region of the current render target with a solid color (M2 Slice D2). Valid only
    // between BeginFrame and EndFrame. The rectangle is in normalized viewport coordinates (see ViewportRect)
    // and the color is renderer-neutral RGBA; no D3D12 types leak through this API, and the renderer assigns
    // it no scene meaning — it simply paints a generic rectangle on the current render target. Malformed
    // input is rejected as a recoverable error (see ValidateViewportRect).
    [[nodiscard]] std::expected<void, std::string> ClearViewportRect(
        const ViewportRect& rect,
        const RgbaColor& color);

    void BeginFrame();
    // Completes the scene-linear pass using the selected SDR or HDR scRGB presentation policy. This must be
    // called after scene draws and before debug/UI draws.
    void OutputSceneToDisplay();
    void EndFrame();

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] bool IsModelPipelineReady() const noexcept;
    [[nodiscard]] bool IsDepthBufferReady() const noexcept;
    [[nodiscard]] float AspectRatio() const noexcept;
    [[nodiscard]] ID3D12Device* Device() const noexcept;
    [[nodiscard]] ID3D12GraphicsCommandList* CommandList() const noexcept;
    [[nodiscard]] ID3D12DescriptorHeap* ImGuiDescriptorHeap() const noexcept;
    [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE ImGuiCpuHandle() const noexcept;
    [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE ImGuiGpuHandle() const noexcept;
    [[nodiscard]] DXGI_FORMAT BackBufferFormat() const noexcept;
    [[nodiscard]] DisplayOutputMode OutputMode() const noexcept;
    [[nodiscard]] float HdrUiReferenceWhiteScale() const noexcept;
    [[nodiscard]] std::uint32_t FrameCount() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}
