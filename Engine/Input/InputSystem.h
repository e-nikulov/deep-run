#pragma once

#include "Engine/Input/GamepadVibration.h"
#include "Engine/Input/InputState.h"
#include "Engine/Platform/Window.h"

#include <memory>
#include <span>

namespace DeepRun::Diagnostics
{
class Logger;
}

namespace DeepRun::Input
{
namespace Windows
{
class WindowsGamingInputGamepad;
}

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
    explicit InputSystem(
        Diagnostics::Logger& logger,
        bool platformBackendEnabled = true,
        void* nativeWindowHandle = nullptr);
    ~InputSystem();

    void BeginFrame();
    void ProcessEvents(std::span<const Platform::WindowEvent> events);
    void UpdateController();
    // Device absence is a normal silent state. False is reserved for malformed normalized input or an
    // unexpected backend failure; neither condition feeds back into simulation.
    [[nodiscard]] bool ApplyGamepadVibration(const GamepadVibration& vibration) noexcept;

    [[nodiscard]] bool WasPressed(InputAction action) const noexcept;
    [[nodiscard]] bool IsControllerConnected() const noexcept;
    [[nodiscard]] bool IsMouseButtonDown(Platform::MouseButton button) const noexcept;
    [[nodiscard]] int MouseX() const noexcept;
    [[nodiscard]] int MouseY() const noexcept;
    [[nodiscard]] const InputState& State() const noexcept;

private:
    void RefreshSemanticAxes() noexcept;

    Diagnostics::Logger& logger_;
    std::unique_ptr<Windows::WindowsGamingInputGamepad> windowsGamepad_;
    InputState state_;
    bool platformBackendEnabled_ = true;
    bool controllerConnected_ = false;
    bool throttleAsternKeyDown_ = false;
    bool throttleAheadKeyDown_ = false;
    bool depthSurfaceKeyDown_ = false;
    bool depthDiveKeyDown_ = false;
};
}
