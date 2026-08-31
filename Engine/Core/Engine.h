#pragma once

#include "Engine/Core/Time.h"

#include <filesystem>
#include <expected>
#include <functional>
#include <memory>
#include <string>

namespace DeepRun::Assets
{
class AssetManager;
}

namespace DeepRun::Physics
{
class PhysicsWorld;
}

namespace DeepRun::Input
{
struct HapticEffectRequest;
class InputState;
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
    using FixedUpdateHook = std::function<bool(float)>;
    using RenderHook = std::function<bool(Render::D3D12Renderer&)>;

    explicit Engine(EngineOptions options);
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    [[nodiscard]] bool Initialize();
    [[nodiscard]] bool Update(const FixedUpdateHook& fixedUpdateHook = {});
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

    // Read-only view of the stable frame input state. It is available only for initialized windowed engines;
    // callers keep no ownership and must not retain it past the Engine's lifetime.
    [[nodiscard]] const Input::InputState* InputState() const noexcept;

    // Generic presentation submission. The Engine understands effect IDs, motors, duration and priority;
    // semantic event meaning remains entirely above this boundary in Game.
    [[nodiscard]] std::expected<void, std::string> SubmitHapticEffect(
        const Input::HapticEffectRequest& request);

    [[nodiscard]] int ExitCode() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}
