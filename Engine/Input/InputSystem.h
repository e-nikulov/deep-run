#pragma once

#include "Engine/Input/InputState.h"
#include "Engine/Platform/Window.h"

#include <span>

namespace DeepRun::Diagnostics
{
class Logger;
}

namespace DeepRun::Input
{
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
    [[nodiscard]] const InputState& State() const noexcept;

private:
    Diagnostics::Logger& logger_;
    InputState state_;
    bool controllerConnected_ = false;
    bool controllerStateKnown_ = false;
};
}
