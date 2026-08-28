#pragma once

#include "Engine/Core/Time.h"

#include <filesystem>
#include <memory>

namespace DeepRun::Assets
{
class AssetManager;
}

namespace DeepRun::Scene
{
class Scene;
}

namespace DeepRun::Core
{
struct EngineOptions final
{
    bool headless = false;
    bool smokeTest = false;
    std::filesystem::path configPath;
    std::filesystem::path contentRoot;
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
    explicit Engine(EngineOptions options);
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    [[nodiscard]] bool Initialize();
    [[nodiscard]] bool Update();
    void Render();
    void RequestShutdown() noexcept;
    void Shutdown() noexcept;

    [[nodiscard]] EngineLifecycle Lifecycle() const noexcept;
    [[nodiscard]] const FrameState& CurrentFrame() const noexcept;
    [[nodiscard]] Scene::Scene& ActiveScene() noexcept;
    [[nodiscard]] Assets::AssetManager& Assets() noexcept;
    [[nodiscard]] int ExitCode() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}
