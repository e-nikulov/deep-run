#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace DeepRun::Diagnostics
{
class Logger;
}

namespace DeepRun::Platform
{
enum class Key
{
    Unknown,
    Escape,
    F1,
};

enum class MouseButton
{
    Left,
    Right,
    Middle,
};

enum class WindowEventType
{
    Close,
    Resized,
    Minimized,
    Restored,
    KeyDown,
    KeyUp,
    MouseMove,
    MouseButtonDown,
    MouseButtonUp,
};

struct WindowEvent
{
    WindowEventType type = WindowEventType::Close;
    Key key = Key::Unknown;
    MouseButton mouseButton = MouseButton::Left;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    int mouseX = 0;
    int mouseY = 0;
    bool repeated = false;
};

struct WindowConfig
{
    std::string title = "DeepRun Engine";
    std::uint32_t width = 1280;
    std::uint32_t height = 720;
};

class Window final
{
public:
    Window(Diagnostics::Logger& logger, const WindowConfig& config);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    [[nodiscard]] std::span<const WindowEvent> PumpEvents();
    void WaitForEvents() const;
    void RequestClose();
    void SetClientSize(std::uint32_t width, std::uint32_t height);

    [[nodiscard]] bool ShouldClose() const noexcept;
    [[nodiscard]] bool Minimized() const noexcept;
    [[nodiscard]] std::uint32_t Width() const noexcept;
    [[nodiscard]] std::uint32_t Height() const noexcept;
    [[nodiscard]] void* NativeHandle() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}
