#include "Engine/Input/InputSystem.h"

#include "Engine/Diagnostics/Logger.h"
#include "Engine/Input/Windows/WindowsGamingInputGamepad.h"

#include <algorithm>
#include <cmath>

namespace DeepRun::Input
{
ControllerSemanticAxes MapControllerLeftStick(const float normalizedLeftX, const float normalizedLeftY) noexcept
{
    if (!std::isfinite(normalizedLeftX) || !std::isfinite(normalizedLeftY))
    {
        return {};
    }

    // Preserve the accepted normalized threshold derived from the former XInput backend (7849 / 32767).
    // The physical API is no longer part of this pure radial dead-zone contract.
    constexpr float LeftThumbDeadZone = 7'849.0F / 32'767.0F;
    const float clampedX = std::clamp(normalizedLeftX, -1.0F, 1.0F);
    const float clampedY = std::clamp(normalizedLeftY, -1.0F, 1.0F);
    const float magnitude = std::sqrt(clampedX * clampedX + clampedY * clampedY);
    if (magnitude <= LeftThumbDeadZone || magnitude <= 0.0F)
    {
        return {};
    }

    const float boundedMagnitude = (std::min)(magnitude, 1.0F);
    const float scaledMagnitude = (boundedMagnitude - LeftThumbDeadZone) / (1.0F - LeftThumbDeadZone);
    const float scale = scaledMagnitude / magnitude;
    return {.throttle = clampedX * scale, .depth = -clampedY * scale};
}

ControllerSemanticAxes SemanticAxesForGamepad(const GamepadState& gamepad) noexcept
{
    return gamepad.connected ? MapControllerLeftStick(gamepad.leftX, gamepad.leftY) : ControllerSemanticAxes{};
}

float ResolveSemanticAxis(
    const bool negativeKeyboardDown,
    const bool positiveKeyboardDown,
    const float controllerValue) noexcept
{
    // Any held direction owns that semantic axis. Opposite held keys intentionally produce a keyboard-owned
    // neutral command instead of leaking a controller value through.
    if (negativeKeyboardDown || positiveKeyboardDown)
    {
        return (positiveKeyboardDown ? 1.0F : 0.0F) - (negativeKeyboardDown ? 1.0F : 0.0F);
    }
    return std::isfinite(controllerValue) ? std::clamp(controllerValue, -1.0F, 1.0F) : 0.0F;
}

InputSystem::InputSystem(
    Diagnostics::Logger& logger,
    const bool platformBackendEnabled,
    void* nativeWindowHandle)
    : logger_(logger), platformBackendEnabled_(platformBackendEnabled)
{
    if (platformBackendEnabled_)
    {
        auto backend = std::make_unique<Windows::WindowsGamingInputGamepad>(logger_, nativeWindowHandle);
        if (backend->Initialize())
        {
            windowsGamepad_ = std::move(backend);
        }
    }
    logger_.Info(Diagnostics::LogCategory::Input, "Input system initialized");
}

InputSystem::~InputSystem()
{
    // Ordinary shutdown always requests exact silence before the physical Windows backend disappears.
    static_cast<void>(ApplyGamepadVibration({}));
    logger_.Info(Diagnostics::LogCategory::Input, "Input system shut down");
}

void InputSystem::BeginFrame()
{
    state_.BeginFrame();
}

void InputSystem::ProcessEvents(const std::span<const Platform::WindowEvent> events)
{
    for (const Platform::WindowEvent& event : events)
    {
        if (event.type == Platform::WindowEventType::KeyDown && !event.repeated)
        {
            if (event.key == Platform::Key::Escape)
            {
                state_.SetActionDown(InputAction::Quit, true);
            }
            else if (event.key == Platform::Key::F1)
            {
                state_.SetActionDown(InputAction::ToggleDebugUi, true);
            }
            else if (event.key == Platform::Key::A)
            {
                throttleAsternKeyDown_ = true;
            }
            else if (event.key == Platform::Key::D)
            {
                throttleAheadKeyDown_ = true;
            }
            else if (event.key == Platform::Key::W)
            {
                depthSurfaceKeyDown_ = true;
            }
            else if (event.key == Platform::Key::S)
            {
                depthDiveKeyDown_ = true;
            }
        }
        else if (event.type == Platform::WindowEventType::KeyUp)
        {
            if (event.key == Platform::Key::Escape)
            {
                state_.SetActionDown(InputAction::Quit, false);
            }
            else if (event.key == Platform::Key::F1)
            {
                state_.SetActionDown(InputAction::ToggleDebugUi, false);
            }
            else if (event.key == Platform::Key::A)
            {
                throttleAsternKeyDown_ = false;
            }
            else if (event.key == Platform::Key::D)
            {
                throttleAheadKeyDown_ = false;
            }
            else if (event.key == Platform::Key::W)
            {
                depthSurfaceKeyDown_ = false;
            }
            else if (event.key == Platform::Key::S)
            {
                depthDiveKeyDown_ = false;
            }
        }
        else if (event.type == Platform::WindowEventType::MouseMove)
        {
            state_.SetMousePosition(event.mouseX, event.mouseY);
        }
        else if (event.type == Platform::WindowEventType::MouseButtonDown ||
                 event.type == Platform::WindowEventType::MouseButtonUp)
        {
            state_.SetMouseButtonDown(
                static_cast<std::size_t>(event.mouseButton),
                event.type == Platform::WindowEventType::MouseButtonDown);
        }
    }
    RefreshSemanticAxes();
}

void InputSystem::UpdateController()
{
    if (!platformBackendEnabled_)
    {
        state_.SetGamepad({});
        RefreshSemanticAxes();
        controllerConnected_ = false;
        return;
    }

    const GamepadState gamepad = windowsGamepad_ ? windowsGamepad_->Poll() : GamepadState{};
    state_.SetGamepad(gamepad);
    RefreshSemanticAxes();
    controllerConnected_ = gamepad.connected;
}

bool InputSystem::ApplyGamepadVibration(const GamepadVibration& vibration) noexcept
{
    const auto validMotor = [](const float value) {
        return std::isfinite(value) && value >= 0.0F && value <= 1.0F;
    };
    if (!validMotor(vibration.lowFrequencyMotor) || !validMotor(vibration.highFrequencyMotor))
    {
        return false;
    }
    if (!platformBackendEnabled_ || !windowsGamepad_)
    {
        return true;
    }
    return windowsGamepad_->ApplyVibration(vibration);
}

void InputSystem::RefreshSemanticAxes() noexcept
{
    const ControllerSemanticAxes controller = SemanticAxesForGamepad(state_.Gamepad());
    state_.SetAxis(
        InputAxis::Throttle,
        ResolveSemanticAxis(throttleAsternKeyDown_, throttleAheadKeyDown_, controller.throttle));
    state_.SetAxis(
        InputAxis::Depth,
        ResolveSemanticAxis(depthSurfaceKeyDown_, depthDiveKeyDown_, controller.depth));
}

bool InputSystem::WasPressed(const InputAction action) const noexcept
{
    return state_.WasPressed(action);
}

bool InputSystem::IsControllerConnected() const noexcept
{
    return controllerConnected_;
}

bool InputSystem::IsMouseButtonDown(const Platform::MouseButton button) const noexcept
{
    return state_.IsMouseButtonDown(static_cast<std::size_t>(button));
}

int InputSystem::MouseX() const noexcept
{
    return state_.MouseX();
}

int InputSystem::MouseY() const noexcept
{
    return state_.MouseY();
}

const InputState& InputSystem::State() const noexcept
{
    return state_;
}
}
