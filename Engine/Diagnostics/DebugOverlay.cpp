#include "Engine/Diagnostics/DebugOverlay.h"

#include "Engine/Diagnostics/Logger.h"
#include "Engine/Platform/Window.h"
#include "Engine/Render/D3D12Renderer.h"

#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>

#include <cmath>
#include <memory>

namespace DeepRun::Diagnostics
{
namespace
{
float SrgbToLinear(const float value) noexcept
{
    return value <= 0.04045F ? value / 12.92F : std::pow((value + 0.055F) / 1.055F, 2.4F);
}

void ConfigureHdrDebugUi(const float referenceWhiteScale)
{
    // ImGui's stock style values are SDR sRGB. The HDR scRGB target is linear, so convert the temporary
    // developer style once and anchor white to the renderer's fixed SDR reference white. This is explicitly
    // not a final HDR-aware shipping UI composition system.
    ImGuiStyle& style = ImGui::GetStyle();
    for (ImVec4& color : style.Colors)
    {
        color.x = SrgbToLinear(color.x) * referenceWhiteScale;
        color.y = SrgbToLinear(color.y) * referenceWhiteScale;
        color.z = SrgbToLinear(color.z) * referenceWhiteScale;
    }
}
}

class DebugOverlay::Impl final
{
public:
    explicit Impl(Logger& logger)
        : logger(logger)
    {
    }

    ~Impl()
    {
        if (initialized)
        {
            ImGui_ImplDX12_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
            logger.Info(LogCategory::Render, "Dear ImGui debug overlay shut down");
        }
    }

    Logger& logger;
    bool initialized = false;
    bool visible = true;
};

DebugOverlay::DebugOverlay(Logger& logger)
    : impl_(std::make_unique<Impl>(logger))
{
}

DebugOverlay::~DebugOverlay() = default;

bool DebugOverlay::Initialize(Platform::Window& window, Render::D3D12Renderer& renderer)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    if (renderer.OutputMode() == Render::DisplayOutputMode::HdrScRgb)
    {
        ConfigureHdrDebugUi(renderer.HdrUiReferenceWhiteScale());
    }
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;

    if (!ImGui_ImplWin32_Init(window.NativeHandle()))
    {
        impl_->logger.Error(LogCategory::Render, "Dear ImGui Win32 backend initialization failed");
        ImGui::DestroyContext();
        return false;
    }

    if (!ImGui_ImplDX12_Init(
            renderer.Device(),
            static_cast<int>(renderer.FrameCount()),
            renderer.BackBufferFormat(),
            renderer.ImGuiDescriptorHeap(),
            renderer.ImGuiCpuHandle(),
            renderer.ImGuiGpuHandle()))
    {
        impl_->logger.Error(LogCategory::Render, "Dear ImGui D3D12 backend initialization failed");
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        return false;
    }

    impl_->initialized = true;
    impl_->logger.Info(LogCategory::Render, "Dear ImGui debug overlay initialized");
    return true;
}

void DebugOverlay::Toggle() noexcept
{
    impl_->visible = !impl_->visible;
}

void DebugOverlay::BeginFrame()
{
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void DebugOverlay::Draw(const DebugStatus& status)
{
    if (!impl_->visible)
    {
        return;
    }

    ImGui::SetNextWindowPos(ImVec2(16.0F, 16.0F), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(320.0F, 0.0F), ImGuiCond_FirstUseEver);
    ImGui::Begin("DeepRun Engine", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::Text("FPS: %.1f", status.framesPerSecond);
    ImGui::Text("Frame time: %.2f ms", status.frameMilliseconds);
    ImGui::Text("Elapsed: %.2f s", status.elapsedSeconds);
    ImGui::Text("Frame: %llu", static_cast<unsigned long long>(status.frameIndex));
    ImGui::Text("Entities: %zu", status.entityCount);
    ImGui::Text("Resources: %zu", status.resourceCount);
    ImGui::Separator();
    ImGui::Text("Renderer: %s", status.rendererReady ? "OK" : "ERROR");
    ImGui::Text("Physics: %s", status.physicsReady ? "OK" : "ERROR");
    ImGui::Text("Audio: %s", status.audioReady ? "OK" : "ERROR");
    ImGui::Text("Controller: %s", status.controllerConnected ? "connected" : "disconnected");
    ImGui::End();
}

void DebugOverlay::Render(ID3D12GraphicsCommandList* commandList)
{
    ImGui::Render();
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
}

bool DebugOverlay::IsInitialized() const noexcept
{
    return impl_->initialized;
}
}
