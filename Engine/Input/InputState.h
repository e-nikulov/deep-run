#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace DeepRun::Input
{
enum class InputAction
{
    Quit,
    ToggleDebugUi,
    Count,
};

// Continuous, gameplay-facing input. These are normalized semantic commands rather than device axes.
enum class InputAxis
{
    Throttle,
    Depth,
    Count,
};

enum class GamepadButton : std::uint16_t
{
    DpadUp = 1U << 0U,
    DpadDown = 1U << 1U,
    DpadLeft = 1U << 2U,
    DpadRight = 1U << 3U,
    Start = 1U << 4U,
    Back = 1U << 5U,
    LeftStick = 1U << 6U,
    RightStick = 1U << 7U,
    LeftShoulder = 1U << 8U,
    RightShoulder = 1U << 9U,
    A = 1U << 12U,
    B = 1U << 13U,
    X = 1U << 14U,
    Y = 1U << 15U,
};

struct GamepadState final
{
    bool connected = false;
    float leftX = 0.0F;
    float leftY = 0.0F;
    float rightX = 0.0F;
    float rightY = 0.0F;
    float leftTrigger = 0.0F;
    float rightTrigger = 0.0F;
    std::uint16_t buttons = 0;
};

class InputState final
{
public:
    void BeginFrame() noexcept;
    void SetActionDown(InputAction action, bool down) noexcept;
    void SetMouseButtonDown(std::size_t button, bool down) noexcept;
    void SetMousePosition(int x, int y) noexcept;
    void SetGamepad(GamepadState gamepad) noexcept;
    void SetAxis(InputAxis axis, float value) noexcept;

    [[nodiscard]] bool IsDown(InputAction action) const noexcept;
    [[nodiscard]] bool WasPressed(InputAction action) const noexcept;
    [[nodiscard]] bool WasReleased(InputAction action) const noexcept;
    [[nodiscard]] bool IsMouseButtonDown(std::size_t button) const noexcept;
    [[nodiscard]] int MouseX() const noexcept;
    [[nodiscard]] int MouseY() const noexcept;
    [[nodiscard]] const GamepadState& Gamepad() const noexcept;
    [[nodiscard]] float Axis(InputAxis axis) const noexcept;

private:
    static constexpr std::size_t ActionCount = static_cast<std::size_t>(InputAction::Count);
    static constexpr std::size_t AxisCount = static_cast<std::size_t>(InputAxis::Count);
    std::array<bool, ActionCount> down_{};
    std::array<bool, ActionCount> pressed_{};
    std::array<bool, ActionCount> released_{};
    std::array<bool, 3> mouseButtons_{};
    std::array<float, AxisCount> axes_{};
    GamepadState gamepad_{};
    int mouseX_ = 0;
    int mouseY_ = 0;
};
}
