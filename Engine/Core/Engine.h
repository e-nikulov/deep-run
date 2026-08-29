#pragma once

#include "Engine/Core/Time.h"

#include <filesystem>
#include <functional>
#include <memory>

namespace DeepRun::Assets
{
class AssetManager;
}

namespace DeepRun::Physics
{
class PhysicsWorld;
}

namespace DeepRun::Scene
{
class Scene;
}

namespace DeepRun::Render
{
class D3D12Renderer;
}

namespace DeepRun::Core
{
struct EngineOptions final
{
    bool headless = false;
    bool smokeTest = false;
    std::filesystem::path configPath;
    std::filesystem::path contentRoot;
    std::filesystem::path shaderRoot;
};

enum class EngineLifecycle
{
    Stopped,
    Initializing,
    Running,
    ShutdownRequested,
};

class Engine final
{
public:
    using RenderHook = std::function<bool(Render::D3D12Renderer&)>;

    explicit Engine(EngineOptions options);
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    [[nodiscard]] bool Initialize();
    [[nodiscard]] bool Update();
    [[nodiscard]] bool Render(const RenderHook& renderHook = {});
    void RequestShutdown() noexcept;
    void Shutdown() noexcept;

    [[nodiscard]] EngineLifecycle Lifecycle() const noexcept;
    [[nodiscard]] const FrameState& CurrentFrame() const noexcept;
    [[nodiscard]] Scene::Scene& ActiveScene() noexcept;
    [[nodiscard]] Assets::AssetManager& Assets() noexcept;
    [[nodiscard]] Render::D3D12Renderer* Renderer() noexcept;

    // Generic accessor to the engine-owned physics subsystem. The Engine owns the PhysicsWorld and
    // outlives every gameplay consumer during Application::Run; callers must not store the pointer
    // beyond that lifetime or create shared ownership of it.
    [[nodiscard]] Physics::PhysicsWorld* Physics() noexcept;

    [[nodiscard]] int ExitCode() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}
