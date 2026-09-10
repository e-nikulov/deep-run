#include "Engine/Input/InputState.h"

#include <algorithm>
#include <cmath>

namespace DeepRun::Input
{
void InputState::BeginFrame() noexcept
{
    std::ranges::fill(pressed_, false);
    std::ranges::fill(released_, false);
    cameraZoomSteps_ = 0.0F;
}

void InputState::SetActionDown(const InputAction action, const bool down) noexcept
{
    const std::size_t index = static_cast<std::size_t>(action);
    if (index >= ActionCount || down_[index] == down)
    {
        return;
    }
    down_[index] = down;
    pressed_[index] = down;
    released_[index] = !down;
}

void InputState::SetMouseButtonDown(const std::size_t button, const bool down) noexcept
{
    if (button < mouseButtons_.size())
    {
        mouseButtons_[button] = down;
    }
}

void InputState::SetMousePosition(const int x, const int y) noexcept
{
    mouseX_ = x;
    mouseY_ = y;
}

void InputState::AddCameraZoomSteps(const float steps) noexcept
{
    if (std::isfinite(steps))
    {
        cameraZoomSteps_ += steps;
    }
}

void InputState::SetGamepad(GamepadState gamepad) noexcept
{
    gamepad_ = gamepad;
}

void InputState::SetAxis(const InputAxis axis, const float value) noexcept
{
    const std::size_t index = static_cast<std::size_t>(axis);
    if (index < AxisCount)
    {
        axes_[index] = std::isfinite(value) ? std::clamp(value, -1.0F, 1.0F) : 0.0F;
    }
}

bool InputState::IsDown(const InputAction action) const noexcept
{
    return down_[static_cast<std::size_t>(action)];
}

bool InputState::WasPressed(const InputAction action) const noexcept
{
    return pressed_[static_cast<std::size_t>(action)];
}

bool InputState::WasReleased(const InputAction action) const noexcept
{
    return released_[static_cast<std::size_t>(action)];
}

bool InputState::IsMouseButtonDown(const std::size_t button) const noexcept
{
    return button < mouseButtons_.size() && mouseButtons_[button];
}

int InputState::MouseX() const noexcept
{
    return mouseX_;
}

int InputState::MouseY() const noexcept
{
    return mouseY_;
}

float InputState::CameraZoomSteps() const noexcept
{
    return cameraZoomSteps_;
}

const GamepadState& InputState::Gamepad() const noexcept
{
    return gamepad_;
}

float InputState::Axis(const InputAxis axis) const noexcept
{
    const std::size_t index = static_cast<std::size_t>(axis);
    return index < AxisCount ? axes_[index] : 0.0F;
}
}
