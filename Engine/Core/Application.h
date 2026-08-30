#pragma once

#include <functional>
#include <span>
#include <string_view>

namespace DeepRun::Render
{
class D3D12Renderer;
}

namespace DeepRun::Core
{
class Engine;

struct ApplicationOptions
{
    bool headless = false;
    bool smokeTest = false;

    [[nodiscard]] static ApplicationOptions Parse(std::span<const std::string_view> arguments);
};

class Application final
{
public:
    using StartupHook = std::function<bool(Engine&)>;
    using FixedUpdateHook = std::function<bool(float)>;
    using RenderHook = std::function<bool(Render::D3D12Renderer&)>;

    explicit Application(
        ApplicationOptions options,
        StartupHook startupHook = {},
        FixedUpdateHook fixedUpdateHook = {},
        RenderHook renderHook = {});
    [[nodiscard]] int Run();

private:
    ApplicationOptions options_;
    StartupHook startupHook_;
    FixedUpdateHook fixedUpdateHook_;
    RenderHook renderHook_;
};
}
