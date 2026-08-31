#include "Engine/Input/Windows/WindowsGamingInputGamepad.h"

#include "Engine/Diagnostics/Logger.h"

#include <Windows.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Gaming.Input.h>
#include <winrt/base.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <string>

namespace DeepRun::Input::Windows
{
namespace
{
// Windows.Gaming.Input GamepadButtons values. They are converted here and never escape this private backend.
constexpr std::uint32_t WgiMenu = 1U << 0U;
constexpr std::uint32_t WgiView = 1U << 1U;
constexpr std::uint32_t WgiA = 1U << 2U;
constexpr std::uint32_t WgiB = 1U << 3U;
constexpr std::uint32_t WgiX = 1U << 4U;
constexpr std::uint32_t WgiY = 1U << 5U;
constexpr std::uint32_t WgiDpadUp = 1U << 6U;
constexpr std::uint32_t WgiDpadDown = 1U << 7U;
constexpr std::uint32_t WgiDpadLeft = 1U << 8U;
constexpr std::uint32_t WgiDpadRight = 1U << 9U;
constexpr std::uint32_t WgiLeftShoulder = 1U << 10U;
constexpr std::uint32_t WgiRightShoulder = 1U << 11U;
constexpr std::uint32_t WgiLeftThumbstick = 1U << 12U;
constexpr std::uint32_t WgiRightThumbstick = 1U << 13U;

std::uint16_t MapButtons(const std::uint32_t buttons) noexcept
{
    std::uint16_t result = 0;
    const auto copy = [&result, buttons](const std::uint32_t source, const GamepadButton target) {
        if ((buttons & source) != 0)
        {
            result |= static_cast<std::uint16_t>(target);
        }
    };
    copy(WgiDpadUp, GamepadButton::DpadUp);
    copy(WgiDpadDown, GamepadButton::DpadDown);
    copy(WgiDpadLeft, GamepadButton::DpadLeft);
    copy(WgiDpadRight, GamepadButton::DpadRight);
    copy(WgiMenu, GamepadButton::Start);
    copy(WgiView, GamepadButton::Back);
    copy(WgiLeftThumbstick, GamepadButton::LeftStick);
    copy(WgiRightThumbstick, GamepadButton::RightStick);
    copy(WgiLeftShoulder, GamepadButton::LeftShoulder);
    copy(WgiRightShoulder, GamepadButton::RightShoulder);
    copy(WgiA, GamepadButton::A);
    copy(WgiB, GamepadButton::B);
    copy(WgiX, GamepadButton::X);
    copy(WgiY, GamepadButton::Y);
    return result;
}

bool IsUsable(const WindowsGamepadReading& reading) noexcept
{
    return std::isfinite(reading.leftX) && std::isfinite(reading.leftY) && std::isfinite(reading.rightX) &&
           std::isfinite(reading.rightY) && std::isfinite(reading.leftTrigger) &&
           std::isfinite(reading.rightTrigger);
}

bool IsZero(const GamepadVibration& vibration) noexcept
{
    return vibration.lowFrequencyMotor == 0.0F && vibration.highFrequencyMotor == 0.0F;
}
}

GamepadState PublishWindowsGamepadState(
    const bool connected,
    const bool focused,
    const std::optional<WindowsGamepadReading> reading) noexcept
{
    if (!connected)
    {
        return {};
    }

    GamepadState result{.connected = true};
    if (!focused || !reading || !IsUsable(*reading))
    {
        return result;
    }

    result.leftX = static_cast<float>(std::clamp(reading->leftX, -1.0, 1.0));
    result.leftY = static_cast<float>(std::clamp(reading->leftY, -1.0, 1.0));
    result.rightX = static_cast<float>(std::clamp(reading->rightX, -1.0, 1.0));
    result.rightY = static_cast<float>(std::clamp(reading->rightY, -1.0, 1.0));
    result.leftTrigger = static_cast<float>(std::clamp(reading->leftTrigger, 0.0, 1.0));
    result.rightTrigger = static_cast<float>(std::clamp(reading->rightTrigger, 0.0, 1.0));
    result.buttons = MapButtons(reading->buttons);
    return result;
}

WindowsGamepadVibrationOutput MapWindowsGamepadVibration(const GamepadVibration& vibration) noexcept
{
    return {
        .leftMotor = static_cast<double>(vibration.lowFrequencyMotor),
        .rightMotor = static_cast<double>(vibration.highFrequencyMotor),
        .leftTrigger = 0.0,
        .rightTrigger = 0.0};
}

class WindowsGamingInputGamepad::Impl final
{
public:
    Impl(Diagnostics::Logger& logger, void* windowHandle)
        : logger_(logger), windowHandle_(static_cast<HWND>(windowHandle))
    {
    }

    ~Impl()
    {
        Shutdown();
    }

    bool Initialize() noexcept
    {
        if (initialized_)
        {
            return true;
        }
        if (windowHandle_ == nullptr)
        {
            LogInitializationFailure("gameplay window handle is unavailable");
            return false;
        }

        try
        {
            // The engine main thread is already COM-initialized as MTA by the audio subsystem (miniaudio) before
            // input starts. Windows.Gaming.Input works from a multithreaded apartment, and repeated MTA
            // initializations are refcounted rather than mode conflicts, so aligning with MTA keeps WGI usable in
            // both windowed and future audio-less configurations. The collection-change callbacks only set an
            // atomic flag and the poll/vibration calls run on this game thread, neither of which requires STA.
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            apartmentInitialized_ = true;
            addedToken_ = winrt::Windows::Gaming::Input::Gamepad::GamepadAdded(
                [this](auto const&, auto const&) { collectionChanged_.store(true, std::memory_order_release); });
            removedToken_ = winrt::Windows::Gaming::Input::Gamepad::GamepadRemoved(
                [this](auto const&, auto const&) { collectionChanged_.store(true, std::memory_order_release); });
            subscribed_ = true;
            initialized_ = true;
            collectionChanged_.store(true, std::memory_order_release);
            RefreshController();
            return true;
        }
        catch (const winrt::hresult_error& error)
        {
            LogInitializationFailure(winrt::to_string(error.message()));
        }
        catch (const std::exception& error)
        {
            LogInitializationFailure(error.what());
        }
        Shutdown();
        return false;
    }

    GamepadState Poll() noexcept
    {
        if (!initialized_)
        {
            return {};
        }

        try
        {
            if (collectionChanged_.exchange(false, std::memory_order_acq_rel) || !gamepad_)
            {
                RefreshController();
            }
            if (!gamepad_)
            {
                return {};
            }

            const bool focused = GetForegroundWindow() == windowHandle_;
            if (!focused)
            {
                return PublishWindowsGamepadState(true, false, std::nullopt);
            }

            const winrt::Windows::Gaming::Input::GamepadReading native = gamepad_.GetCurrentReading();
            return PublishWindowsGamepadState(
                true,
                true,
                WindowsGamepadReading{
                    .leftX = native.LeftThumbstickX,
                    .leftY = native.LeftThumbstickY,
                    .rightX = native.RightThumbstickX,
                    .rightY = native.RightThumbstickY,
                    .leftTrigger = native.LeftTrigger,
                    .rightTrigger = native.RightTrigger,
                    .buttons = static_cast<std::uint32_t>(native.Buttons)});
        }
        catch (const winrt::hresult_error& error)
        {
            HandleRuntimeFailure("controller reading failed", error.message());
            return {};
        }
        catch (const std::exception& error)
        {
            HandleRuntimeFailure("controller reading failed", winrt::to_hstring(error.what()));
            return {};
        }
    }

    bool ApplyVibration(const GamepadVibration& vibration) noexcept
    {
        if (!initialized_ || !gamepad_)
        {
            return true;
        }

        try
        {
            // Non-zero presentation output is foreground-only. Exact zero is always forwarded so minimize,
            // shutdown, focus loss, and device cleanup cannot leave the physical motors active.
            const GamepadVibration effective = GetForegroundWindow() == windowHandle_ || IsZero(vibration)
                                                   ? vibration
                                                   : GamepadVibration{};
            const WindowsGamepadVibrationOutput mapped = MapWindowsGamepadVibration(effective);
            winrt::Windows::Gaming::Input::GamepadVibration native{};
            native.LeftMotor = mapped.leftMotor;
            native.RightMotor = mapped.rightMotor;
            native.LeftTrigger = mapped.leftTrigger;
            native.RightTrigger = mapped.rightTrigger;
            gamepad_.Vibration(native);
            return true;
        }
        catch (const winrt::hresult_error& error)
        {
            HandleRuntimeFailure("vibration output failed; haptics are silent", error.message());
        }
        catch (const std::exception& error)
        {
            HandleRuntimeFailure("vibration output failed; haptics are silent", winrt::to_hstring(error.what()));
        }
        return false;
    }

private:
    void RefreshController()
    {
        const auto gamepads = winrt::Windows::Gaming::Input::Gamepad::Gamepads();
        if (gamepad_)
        {
            bool found = false;
            for (const auto& candidate : gamepads)
            {
                if (winrt::get_unknown(candidate) == winrt::get_unknown(gamepad_))
                {
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                ClearController(true);
            }
        }

        if (!gamepad_ && gamepads.Size() > 0)
        {
            gamepad_ = gamepads.GetAt(0);
            logger_.Info(
                Diagnostics::LogCategory::Input,
                "Windows.Gaming.Input controller connected");
        }
    }

    void ClearController(const bool logTransition) noexcept
    {
        if (!gamepad_)
        {
            return;
        }
        StopVibrationNoThrow();
        gamepad_ = nullptr;
        if (logTransition)
        {
            logger_.Info(
                Diagnostics::LogCategory::Input,
                "Windows.Gaming.Input controller disconnected");
        }
    }

    void StopVibrationNoThrow() noexcept
    {
        try
        {
            if (gamepad_)
            {
                winrt::Windows::Gaming::Input::GamepadVibration zero{};
                gamepad_.Vibration(zero);
            }
        }
        catch (...)
        {
        }
    }

    void HandleRuntimeFailure(const char* context, const winrt::hstring& detail) noexcept
    {
        if (!runtimeFailureLogged_)
        {
            runtimeFailureLogged_ = true;
            logger_.Warning(
                Diagnostics::LogCategory::Input,
                std::string("Windows.Gaming.Input ") + context + ": " + winrt::to_string(detail));
        }
        ClearController(true);
        collectionChanged_.store(true, std::memory_order_release);
    }

    void LogInitializationFailure(const std::string& detail) noexcept
    {
        if (!initializationFailureLogged_)
        {
            initializationFailureLogged_ = true;
            logger_.Warning(
                Diagnostics::LogCategory::Input,
                "Windows.Gaming.Input backend unavailable; keyboard input remains active: " + detail);
        }
    }

    void Shutdown() noexcept
    {
        StopVibrationNoThrow();
        if (subscribed_)
        {
            try
            {
                winrt::Windows::Gaming::Input::Gamepad::GamepadAdded(addedToken_);
                winrt::Windows::Gaming::Input::Gamepad::GamepadRemoved(removedToken_);
            }
            catch (...)
            {
            }
            subscribed_ = false;
        }
        gamepad_ = nullptr;
        initialized_ = false;
        if (apartmentInitialized_)
        {
            winrt::uninit_apartment();
            apartmentInitialized_ = false;
        }
    }

    Diagnostics::Logger& logger_;
    HWND windowHandle_ = nullptr;
    winrt::Windows::Gaming::Input::Gamepad gamepad_{nullptr};
    winrt::event_token addedToken_{};
    winrt::event_token removedToken_{};
    std::atomic_bool collectionChanged_ = true;
    bool apartmentInitialized_ = false;
    bool subscribed_ = false;
    bool initialized_ = false;
    bool initializationFailureLogged_ = false;
    bool runtimeFailureLogged_ = false;
};

WindowsGamingInputGamepad::WindowsGamingInputGamepad(Diagnostics::Logger& logger, void* windowHandle)
    : impl_(std::make_unique<Impl>(logger, windowHandle))
{
}

WindowsGamingInputGamepad::~WindowsGamingInputGamepad() = default;

bool WindowsGamingInputGamepad::Initialize() noexcept
{
    return impl_->Initialize();
}

GamepadState WindowsGamingInputGamepad::Poll() noexcept
{
    return impl_->Poll();
}

bool WindowsGamingInputGamepad::ApplyVibration(const GamepadVibration& vibration) noexcept
{
    return impl_->ApplyVibration(vibration);
}
}
