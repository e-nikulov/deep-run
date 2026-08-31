#pragma once

#include "Engine/Input/GamepadVibration.h"
#include "Engine/Input/InputState.h"

#include <cstdint>
#include <memory>
#include <optional>

namespace DeepRun::Diagnostics
{
class Logger;
}

namespace DeepRun::Input::Windows
{
// Private normalized snapshot between the Windows.Gaming.Input projection and the generic Engine state.
// No C++/WinRT type crosses this boundary, so conversion and neutralization remain hardware-free testable.
struct WindowsGamepadReading final
{
    double leftX = 0.0;
    double leftY = 0.0;
    double rightX = 0.0;
    double rightY = 0.0;
    double leftTrigger = 0.0;
    double rightTrigger = 0.0;
    std::uint32_t buttons = 0;
};

struct WindowsGamepadVibrationOutput final
{
    double leftMotor = 0.0;
    double rightMotor = 0.0;
    double leftTrigger = 0.0;
    double rightTrigger = 0.0;

    friend bool operator==(const WindowsGamepadVibrationOutput&, const WindowsGamepadVibrationOutput&) = default;
};

[[nodiscard]] GamepadState PublishWindowsGamepadState(
    bool connected,
    bool focused,
    std::optional<WindowsGamepadReading> reading) noexcept;

[[nodiscard]] WindowsGamepadVibrationOutput MapWindowsGamepadVibration(
    const GamepadVibration& vibration) noexcept;

class WindowsGamingInputGamepad final
{
public:
    WindowsGamingInputGamepad(Diagnostics::Logger& logger, void* windowHandle);
    ~WindowsGamingInputGamepad();

    WindowsGamingInputGamepad(const WindowsGamingInputGamepad&) = delete;
    WindowsGamingInputGamepad& operator=(const WindowsGamingInputGamepad&) = delete;

    [[nodiscard]] bool Initialize() noexcept;
    [[nodiscard]] GamepadState Poll() noexcept;
    [[nodiscard]] bool ApplyVibration(const GamepadVibration& vibration) noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}

