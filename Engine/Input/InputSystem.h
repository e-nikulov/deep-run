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
// Small pure controller boundary used by InputSystem and headless tests. Inputs are already normalized by
// the platform backend; output follows the canonical semantic signs (stick up -> Depth < 0).
struct ControllerSemanticAxes final
{
    float throttle = 0.0F;
    float depth = 0.0F;
};

[[nodiscard]] ControllerSemanticAxes MapControllerLeftStick(float normalizedLeftX, float normalizedLeftY) noexcept;
[[nodiscard]] ControllerSemanticAxes SemanticAxesForGamepad(const GamepadState& gamepad) noexcept;
[[nodiscard]] float ResolveSemanticAxis(
    bool negativeKeyboardDown,
    bool positiveKeyboardDown,
    float controllerValue) noexcept;

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
    void RefreshSemanticAxes() noexcept;

    Diagnostics::Logger& logger_;
    InputState state_;
    bool controllerConnected_ = false;
    bool controllerStateKnown_ = false;
    bool throttleAsternKeyDown_ = false;
    bool throttleAheadKeyDown_ = false;
    bool depthSurfaceKeyDown_ = false;
    bool depthDiveKeyDown_ = false;
};
}
