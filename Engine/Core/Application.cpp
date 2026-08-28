#include "Engine/Core/Application.h"

#include "Engine/Core/Engine.h"
#include "Engine/Platform/Platform.h"

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

Application::Application(const ApplicationOptions options)
    : options_(options)
{
}

int Application::Run()
{
    const std::filesystem::path executableDirectory = Platform::ExecutablePath().parent_path();
    Engine engine({
        .headless = options_.headless,
        .smokeTest = options_.smokeTest,
        .configPath = executableDirectory / "Config" / "engine.json",
        .contentRoot = executableDirectory / "Content"});
    if (!engine.Initialize())
    {
        return engine.ExitCode();
    }

    while (engine.Lifecycle() == EngineLifecycle::Running)
    {
        if (engine.Update())
        {
            engine.Render();
        }
    }
    engine.Shutdown();
    return engine.ExitCode();
}
}
