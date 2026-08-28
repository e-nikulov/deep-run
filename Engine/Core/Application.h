#pragma once

#include <span>
#include <string_view>

namespace DeepRun::Core
{
struct ApplicationOptions
{
    bool headless = false;
    bool smokeTest = false;

    [[nodiscard]] static ApplicationOptions Parse(std::span<const std::string_view> arguments);
};

class Application final
{
public:
    explicit Application(ApplicationOptions options);
    [[nodiscard]] int Run();

private:
    ApplicationOptions options_;
};
}
