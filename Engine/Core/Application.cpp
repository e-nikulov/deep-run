#include "Engine/Core/Application.h"

#include "Engine/Core/Engine.h"
#include "Engine/Platform/Platform.h"

#include <utility>

namespace DeepRun::Core
{
ApplicationOptions ApplicationOptions::Parse(const std::span<const std::string_view> arguments)
{
    ApplicationOptions options;
    for (const std::string_view argument : arguments)
    {
        if (argument == "--headless")
        {
            options.headless = true;
        }
        else if (argument == "--smoke-test")
        {
            options.smokeTest = true;
        }
    }
    return options;
}

Application::Application(
    const ApplicationOptions options,
    StartupHook startupHook,
    FixedUpdateHook fixedUpdateHook,
    RenderHook renderHook)
    : options_(options), startupHook_(std::move(startupHook)), fixedUpdateHook_(std::move(fixedUpdateHook)),
      renderHook_(std::move(renderHook))
{
}

int Application::Run()
{
    const std::filesystem::path executableDirectory = Platform::ExecutablePath().parent_path();
    Engine engine({
        .headless = options_.headless,
        .smokeTest = options_.smokeTest,
        .configPath = executableDirectory / "Config" / "engine.json",
        .contentRoot = executableDirectory / "Content",
        .shaderRoot = executableDirectory / "Shaders"});
    if (!engine.Initialize())
    {
        return engine.ExitCode();
    }
    if (startupHook_ && !startupHook_(engine))
    {
        engine.Shutdown();
        return 9;
    }

    const Engine::FixedUpdateHook activeFixedUpdateHook = options_.headless ? Engine::FixedUpdateHook{} : fixedUpdateHook_;
    while (engine.Lifecycle() == EngineLifecycle::Running)
    {
        // Ordinary --headless keeps its established self-contained physics smoke path. A composition fixed
        // hook is a windowed gameplay concern in the executable; direct headless integration tests use
        // lower-level simulation APIs without constructing the playground or renderer.
        if (engine.Update(activeFixedUpdateHook))
        {
            static_cast<void>(engine.Render(renderHook_));
        }
    }
    engine.Shutdown();
    return engine.ExitCode();
}
}
