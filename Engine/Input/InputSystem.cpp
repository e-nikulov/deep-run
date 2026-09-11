#include "Engine/Input/InputSystem.h"

#include "Engine/Diagnostics/Logger.h"
#include "Engine/Input/Windows/WindowsGamingInputGamepad.h"

#include <algorithm>
#include <cmath>

namespace DeepRun::Input
{
namespace
{
struct StickAxes final
{
    float x = 0.0F;
    float y = 0.0F;
};

[[nodiscard]] StickAxes MapRadialStick(
    const float normalizedX,
    const float normalizedY,
    const float deadZone) noexcept
{
    if (!std::isfinite(normalizedX) || !std::isfinite(normalizedY) ||
        !std::isfinite(deadZone) || deadZone < 0.0F || deadZone >= 1.0F)
    {
        return {};
    }

    const float clampedX = std::clamp(normalizedX, -1.0F, 1.0F);
    const float clampedY = std::clamp(normalizedY, -1.0F, 1.0F);
    const float magnitude = std::sqrt(clampedX * clampedX + clampedY * clampedY);
    if (magnitude <= deadZone || magnitude <= 0.0F)
    {
        return {};
    }

    const float boundedMagnitude = (std::min)(magnitude, 1.0F);
    const float scaledMagnitude = (boundedMagnitude - deadZone) / (1.0F - deadZone);
    const float scale = scaledMagnitude / magnitude;
    return {.x = clampedX * scale, .y = clampedY * scale};
}

[[nodiscard]] bool HasGamepadButton(const GamepadState& gamepad, const GamepadButton button) noexcept
{
    return (gamepad.buttons & static_cast<std::uint16_t>(button)) != 0U;
}
}

ControllerSemanticAxes MapControllerLeftStick(const float normalizedLeftX, const float normalizedLeftY) noexcept
{
    constexpr float LeftThumbDeadZone = 7'849.0F / 32'767.0F;
    const StickAxes mapped = MapRadialStick(normalizedLeftX, normalizedLeftY, LeftThumbDeadZone);
    return {.throttle = mapped.x, .depth = -mapped.y};
}

ControllerSemanticAxes SemanticAxesForGamepad(const GamepadState& gamepad) noexcept
{
    if (!gamepad.connected)
    {
        return {};
    }

    ControllerSemanticAxes result = MapControllerLeftStick(gamepad.leftX, gamepad.leftY);
    constexpr float CameraStickDeadZone = 0.24F;
    const StickAxes camera = MapRadialStick(gamepad.rightX, gamepad.rightY, CameraStickDeadZone);
    result.cameraPanX = camera.x;
    result.cameraPanY = camera.y;

    const float leftTrigger = std::isfinite(gamepad.leftTrigger)
                                  ? std::clamp(gamepad.leftTrigger, 0.0F, 1.0F)
                                  : 0.0F;
    const float rightTrigger = std::isfinite(gamepad.rightTrigger)
                                   ? std::clamp(gamepad.rightTrigger, 0.0F, 1.0F)
                                   : 0.0F;
    result.cameraZoom = rightTrigger - leftTrigger;
    return result;
}

ControllerSemanticActions SemanticActionsForGamepad(const GamepadState& gamepad) noexcept
{
    if (!gamepad.connected)
    {
        return {};
    }

    return ControllerSemanticActions{
        .selectContact = HasGamepadButton(gamepad, GamepadButton::Y),
        .prepareWeapon = HasGamepadButton(gamepad, GamepadButton::X),
        .fireWeapon = HasGamepadButton(gamepad, GamepadButton::A),
        .deployDecoy = HasGamepadButton(gamepad, GamepadButton::B)};
}

float ResolveSemanticAxis(
    const bool negativeKeyboardDown,
    const bool positiveKeyboardDown,
    const float controllerValue) noexcept
{
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
            else if (event.key == Platform::Key::Tab)
            {
                selectContactKeyDown_ = true;
            }
            else if (event.key == Platform::Key::R)
            {
                prepareWeaponKeyDown_ = true;
            }
            else if (event.key == Platform::Key::Space)
            {
                fireWeaponKeyDown_ = true;
            }
            else if (event.key == Platform::Key::F)
            {
                deployDecoyKeyDown_ = true;
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
            else if (event.key == Platform::Key::Left)
            {
                cameraPanLeftKeyDown_ = true;
            }
            else if (event.key == Platform::Key::Right)
            {
                cameraPanRightKeyDown_ = true;
            }
            else if (event.key == Platform::Key::Up)
            {
                cameraPanUpKeyDown_ = true;
            }
            else if (event.key == Platform::Key::Down)
            {
                cameraPanDownKeyDown_ = true;
            }
            else if (event.key == Platform::Key::Q)
            {
                cameraZoomInKeyDown_ = true;
            }
            else if (event.key == Platform::Key::E)
            {
                cameraZoomOutKeyDown_ = true;
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
            else if (event.key == Platform::Key::Tab)
            {
                selectContactKeyDown_ = false;
            }
            else if (event.key == Platform::Key::R)
            {
                prepareWeaponKeyDown_ = false;
            }
            else if (event.key == Platform::Key::Space)
            {
                fireWeaponKeyDown_ = false;
            }
            else if (event.key == Platform::Key::F)
            {
                deployDecoyKeyDown_ = false;
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
            else if (event.key == Platform::Key::Left)
            {
                cameraPanLeftKeyDown_ = false;
            }
            else if (event.key == Platform::Key::Right)
            {
                cameraPanRightKeyDown_ = false;
            }
            else if (event.key == Platform::Key::Up)
            {
                cameraPanUpKeyDown_ = false;
            }
            else if (event.key == Platform::Key::Down)
            {
                cameraPanDownKeyDown_ = false;
            }
            else if (event.key == Platform::Key::Q)
            {
                cameraZoomInKeyDown_ = false;
            }
            else if (event.key == Platform::Key::E)
            {
                cameraZoomOutKeyDown_ = false;
            }
        }
        else if (event.type == Platform::WindowEventType::MouseMove)
        {
            state_.SetMousePosition(event.mouseX, event.mouseY);
        }
        else if (event.type == Platform::WindowEventType::MouseWheel)
        {
            state_.AddCameraZoomSteps(event.mouseWheelSteps);
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
    RefreshSemanticActions();
}

void InputSystem::UpdateController()
{
    if (!platformBackendEnabled_)
    {
        state_.SetGamepad({});
        RefreshSemanticAxes();
        RefreshSemanticActions();
        controllerConnected_ = false;
        return;
    }

    const GamepadState gamepad = windowsGamepad_ ? windowsGamepad_->Poll() : GamepadState{};
    state_.SetGamepad(gamepad);
    RefreshSemanticAxes();
    RefreshSemanticActions();
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
    state_.SetAxis(
        InputAxis::CameraPanX,
        ResolveSemanticAxis(cameraPanLeftKeyDown_, cameraPanRightKeyDown_, controller.cameraPanX));
    state_.SetAxis(
        InputAxis::CameraPanY,
        ResolveSemanticAxis(cameraPanDownKeyDown_, cameraPanUpKeyDown_, controller.cameraPanY));
    state_.SetAxis(
        InputAxis::CameraZoom,
        ResolveSemanticAxis(cameraZoomInKeyDown_, cameraZoomOutKeyDown_, controller.cameraZoom));
}

void InputSystem::RefreshSemanticActions() noexcept
{
    const ControllerSemanticActions controller = SemanticActionsForGamepad(state_.Gamepad());
    state_.SetActionDown(InputAction::SelectContact, selectContactKeyDown_ || controller.selectContact);
    state_.SetActionDown(
        InputAction::PrepareWeapon,
        prepareWeaponKeyDown_ ||
            state_.IsMouseButtonDown(static_cast<std::size_t>(Platform::MouseButton::Right)) ||
            controller.prepareWeapon);
    state_.SetActionDown(
        InputAction::FireWeapon,
        fireWeaponKeyDown_ ||
            state_.IsMouseButtonDown(static_cast<std::size_t>(Platform::MouseButton::Left)) ||
            controller.fireWeapon);
    state_.SetActionDown(InputAction::DeployDecoy, deployDecoyKeyDown_ || controller.deployDecoy);
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
