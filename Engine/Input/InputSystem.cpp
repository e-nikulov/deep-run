#include "Engine/Input/InputSystem.h"

#include "Engine/Diagnostics/Logger.h"

#include <Windows.h>
#include <Xinput.h>

#include <algorithm>

namespace DeepRun::Input
{
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
    std::ranges::fill(pressed_, false);
}

void InputSystem::ProcessEvents(const std::span<const Platform::WindowEvent> events)
{
    for (const Platform::WindowEvent& event : events)
    {
        if (event.type == Platform::WindowEventType::KeyDown && !event.repeated)
        {
            if (event.key == Platform::Key::Escape)
            {
                pressed_[static_cast<std::size_t>(InputAction::Quit)] = true;
            }
            else if (event.key == Platform::Key::F1)
            {
                pressed_[static_cast<std::size_t>(InputAction::ToggleDebugUi)] = true;
            }
        }
        else if (event.type == Platform::WindowEventType::MouseMove)
        {
            mouseX_ = event.mouseX;
            mouseY_ = event.mouseY;
        }
        else if (event.type == Platform::WindowEventType::MouseButtonDown ||
                 event.type == Platform::WindowEventType::MouseButtonUp)
        {
            mouseButtons_[static_cast<std::size_t>(event.mouseButton)] =
                event.type == Platform::WindowEventType::MouseButtonDown;
        }
    }
}

void InputSystem::UpdateController()
{
    XINPUT_STATE state{};
    const bool connected = XInputGetState(0, &state) == ERROR_SUCCESS;
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
    return pressed_[static_cast<std::size_t>(action)];
}

bool InputSystem::IsControllerConnected() const noexcept
{
    return controllerConnected_;
}

bool InputSystem::IsMouseButtonDown(const Platform::MouseButton button) const noexcept
{
    return mouseButtons_[static_cast<std::size_t>(button)];
}

int InputSystem::MouseX() const noexcept
{
    return mouseX_;
}

int InputSystem::MouseY() const noexcept
{
    return mouseY_;
}
}
