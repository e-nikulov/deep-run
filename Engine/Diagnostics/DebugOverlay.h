#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

struct ID3D12GraphicsCommandList;

namespace DeepRun::Platform
{
class Window;
}

namespace DeepRun::Render
{
class D3D12Renderer;
}

namespace DeepRun::Diagnostics
{
class Logger;

struct DebugStatus
{
    double framesPerSecond = 0.0;
    double frameMilliseconds = 0.0;
    double elapsedSeconds = 0.0;
    std::uint64_t frameIndex = 0;
    std::size_t entityCount = 0;
    std::size_t resourceCount = 0;
    bool rendererReady = false;
    bool physicsReady = false;
    bool audioReady = false;
    bool controllerConnected = false;
};

class DebugOverlay final
{
public:
    explicit DebugOverlay(Logger& logger);
    ~DebugOverlay();

    DebugOverlay(const DebugOverlay&) = delete;
    DebugOverlay& operator=(const DebugOverlay&) = delete;

    [[nodiscard]] bool Initialize(Platform::Window& window, Render::D3D12Renderer& renderer);
    void Toggle() noexcept;
    void BeginFrame();
    void Draw(const DebugStatus& status);
    void Render(ID3D12GraphicsCommandList* commandList);
    [[nodiscard]] bool IsInitialized() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}
