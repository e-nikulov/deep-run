#include "Engine/Input/InputSystem.h"

#include "Engine/Diagnostics/Logger.h"

#include <Windows.h>
#include <Xinput.h>

#include <limits>

namespace DeepRun::Input
{
namespace
{
float NormalizeStick(const short value) noexcept
{
    constexpr float PositiveScale = 1.0F / 32'767.0F;
    constexpr float NegativeScale = 1.0F / 32'768.0F;
    return static_cast<float>(value) * (value >= 0 ? PositiveScale : NegativeScale);
}
}

InputSystem::InputSystem(Diagnostics::Logger& logger)
    : logger_(logger)
{
    logger_.Info(Diagnostics::LogCategory::Input, "Input system initialized");
    UpdateController();
}

InputSystem::~InputSystem()
{
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
}

void InputSystem::UpdateController()
{
    XINPUT_STATE state{};
    const bool connected = XInputGetState(0, &state) == ERROR_SUCCESS;
    GamepadState gamepad;
    gamepad.connected = connected;
    if (connected)
    {
        constexpr float TriggerScale = 1.0F / static_cast<float>(std::numeric_limits<unsigned char>::max());
        gamepad.leftX = NormalizeStick(state.Gamepad.sThumbLX);
        gamepad.leftY = NormalizeStick(state.Gamepad.sThumbLY);
        gamepad.rightX = NormalizeStick(state.Gamepad.sThumbRX);
        gamepad.rightY = NormalizeStick(state.Gamepad.sThumbRY);
        gamepad.leftTrigger = static_cast<float>(state.Gamepad.bLeftTrigger) * TriggerScale;
        gamepad.rightTrigger = static_cast<float>(state.Gamepad.bRightTrigger) * TriggerScale;
        gamepad.buttons = state.Gamepad.wButtons;
    }
    state_.SetGamepad(gamepad);
    if (!controllerStateKnown_ || connected != controllerConnected_)
    {
        logger_.Info(
            Diagnostics::LogCategory::Input,
            connected ? "XInput controller connected" : "XInput controller disconnected");
    }
    controllerConnected_ = connected;
    controllerStateKnown_ = true;
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
