#pragma once

#include "Engine/Platform/Window.h"

#include <array>
#include <span>

namespace DeepRun::Diagnostics
{
class Logger;
}

namespace DeepRun::Input
{
enum class InputAction
{
    Quit,
    ToggleDebugUi,
    Count,
};

class InputSystem final
{
public:
    explicit InputSystem(Diagnostics::Logger& logger);
    ~InputSystem();

    void BeginFrame();
    void ProcessEvents(std::span<const Platform::WindowEvent> events);
    void UpdateController();

    [[nodiscard]] bool WasPressed(InputAction action) const noexcept;
    [[nodiscard]] bool IsControllerConnected() const noexcept;
    [[nodiscard]] bool IsMouseButtonDown(Platform::MouseButton button) const noexcept;
    [[nodiscard]] int MouseX() const noexcept;
    [[nodiscard]] int MouseY() const noexcept;

private:
    Diagnostics::Logger& logger_;
    std::array<bool, static_cast<std::size_t>(InputAction::Count)> pressed_{};
    bool controllerConnected_ = false;
    bool controllerStateKnown_ = false;
    std::array<bool, 3> mouseButtons_{};
    int mouseX_ = 0;
    int mouseY_ = 0;
};
}
