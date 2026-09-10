#include "Engine/Platform/Window.h"

#include "Engine/Diagnostics/Logger.h"

#include <Windows.h>
#include <windowsx.h>
#include <imgui.h>
#include <imgui_impl_win32.h>

#include <stdexcept>
#include <utility>
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

namespace DeepRun::Platform
{
namespace
{
constexpr wchar_t WindowClassName[] = L"DeepRunEngineWindow";

std::wstring ToWide(const std::string& text)
{
    if (text.empty())
    {
        return {};
    }
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), length);
    return result;
}

Key TranslateKey(const WPARAM virtualKey)
{
    switch (virtualKey)
    {
    case VK_ESCAPE: return Key::Escape;
    case VK_F1: return Key::F1;
    case VK_TAB: return Key::Tab;
    case 'A': return Key::A;
    case 'D': return Key::D;
    case 'W': return Key::W;
    case 'S': return Key::S;
    case VK_LEFT: return Key::Left;
    case VK_RIGHT: return Key::Right;
    case VK_UP: return Key::Up;
    case VK_DOWN: return Key::Down;
    case 'Q': return Key::Q;
    case 'E': return Key::E;
    default: return Key::Unknown;
    }
}
}

class Window::Impl final
{
public:
    Impl(Diagnostics::Logger& logger, const WindowConfig& config)
        : logger_(logger), width_(config.width), height_(config.height)
    {
        instance_ = GetModuleHandleW(nullptr);

        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.style = CS_HREDRAW | CS_VREDRAW;
        windowClass.lpfnWndProc = WindowProcedure;
        windowClass.hInstance = instance_;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.lpszClassName = WindowClassName;

        if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        {
            throw std::runtime_error("RegisterClassExW failed");
        }

        RECT rectangle{0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_)};
        const DWORD style = config.borderless ? WS_POPUP : WS_OVERLAPPEDWINDOW;
        if (!AdjustWindowRect(&rectangle, style, FALSE))
        {
            throw std::runtime_error("AdjustWindowRect failed");
        }

        const std::wstring title = ToWide(config.title);
        handle_ = CreateWindowExW(
            0,
            WindowClassName,
            title.c_str(),
            style,
            config.borderless ? 0 : CW_USEDEFAULT,
            config.borderless ? 0 : CW_USEDEFAULT,
            rectangle.right - rectangle.left,
            rectangle.bottom - rectangle.top,
            nullptr,
            nullptr,
            instance_,
            this);

        if (handle_ == nullptr)
        {
            throw std::runtime_error("CreateWindowExW failed");
        }

        ShowWindow(handle_, SW_SHOWDEFAULT);
        UpdateWindow(handle_);
        logger_.Info(Diagnostics::LogCategory::Platform, "Win32 window created");
    }

    ~Impl()
    {
        if (handle_ != nullptr)
        {
            DestroyWindow(handle_);
            handle_ = nullptr;
        }
        UnregisterClassW(WindowClassName, instance_);
        logger_.Info(Diagnostics::LogCategory::Platform, "Win32 window shut down");
    }

    std::span<const WindowEvent> PumpEvents()
    {
        frameEvents_.clear();
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            if (message.message == WM_QUIT)
            {
                shouldClose_ = true;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        frameEvents_.swap(pendingEvents_);
        return frameEvents_;
    }

    static LRESULT CALLBACK WindowProcedure(HWND handle, UINT message, WPARAM wParam, LPARAM lParam)
    {
        Impl* window = reinterpret_cast<Impl*>(GetWindowLongPtrW(handle, GWLP_USERDATA));
        if (message == WM_NCCREATE)
        {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            window = static_cast<Impl*>(create->lpCreateParams);
            window->handle_ = handle;
            SetWindowLongPtrW(handle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
        }

        if (ImGui::GetCurrentContext() != nullptr && ImGui_ImplWin32_WndProcHandler(handle, message, wParam, lParam))
        {
            return 1;
        }

        if (window == nullptr)
        {
            return DefWindowProcW(handle, message, wParam, lParam);
        }

        switch (message)
        {
        case WM_CLOSE:
            window->shouldClose_ = true;
            window->pendingEvents_.push_back({.type = WindowEventType::Close});
            DestroyWindow(handle);
            return 0;
        case WM_DESTROY:
            window->handle_ = nullptr;
            PostQuitMessage(0);
            return 0;
        case WM_SIZE:
            window->width_ = LOWORD(lParam);
            window->height_ = HIWORD(lParam);
            if (wParam == SIZE_MINIMIZED)
            {
                window->minimized_ = true;
                window->pendingEvents_.push_back({.type = WindowEventType::Minimized});
            }
            else
            {
                const bool wasMinimized = std::exchange(window->minimized_, false);
                if (wasMinimized)
                {
                    window->pendingEvents_.push_back({.type = WindowEventType::Restored});
                }
                window->pendingEvents_.push_back({
                    .type = WindowEventType::Resized,
                    .width = window->width_,
                    .height = window->height_});
            }
            return 0;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            window->pendingEvents_.push_back({
                .type = WindowEventType::KeyDown,
                .key = TranslateKey(wParam),
                .repeated = (lParam & (1LL << 30)) != 0});
            return 0;
        case WM_KEYUP:
        case WM_SYSKEYUP:
            window->pendingEvents_.push_back({.type = WindowEventType::KeyUp, .key = TranslateKey(wParam)});
            return 0;
        case WM_MOUSEMOVE:
            window->pendingEvents_.push_back({
                .type = WindowEventType::MouseMove,
                .mouseX = GET_X_LPARAM(lParam),
                .mouseY = GET_Y_LPARAM(lParam)});
            return 0;
        case WM_MOUSEWHEEL:
            window->pendingEvents_.push_back({
                .type = WindowEventType::MouseWheel,
                .mouseWheelSteps = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) /
                                   static_cast<float>(WHEEL_DELTA)});
            return 0;
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
            window->PushMouseButtonEvent(WindowEventType::MouseButtonDown, message);
            return 0;
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP:
            window->PushMouseButtonEvent(WindowEventType::MouseButtonUp, message);
            return 0;
        default:
            return DefWindowProcW(handle, message, wParam, lParam);
        }
    }

    void PushMouseButtonEvent(const WindowEventType type, const UINT message)
    {
        MouseButton button = MouseButton::Left;
        if (message == WM_RBUTTONDOWN || message == WM_RBUTTONUP)
        {
            button = MouseButton::Right;
        }
        else if (message == WM_MBUTTONDOWN || message == WM_MBUTTONUP)
        {
            button = MouseButton::Middle;
        }
        pendingEvents_.push_back({.type = type, .mouseButton = button});
    }

    Diagnostics::Logger& logger_;
    HINSTANCE instance_ = nullptr;
    HWND handle_ = nullptr;
    std::vector<WindowEvent> pendingEvents_;
    std::vector<WindowEvent> frameEvents_;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    bool shouldClose_ = false;
    bool minimized_ = false;
};

Window::Window(Diagnostics::Logger& logger, const WindowConfig& config)
    : impl_(std::make_unique<Impl>(logger, config))
{
}

Window::~Window() = default;

std::span<const WindowEvent> Window::PumpEvents()
{
    return impl_->PumpEvents();
}

void Window::WaitForEvents() const
{
    WaitMessage();
}

void Window::RequestClose()
{
    impl_->shouldClose_ = true;
}

void Window::SetClientSize(const std::uint32_t width, const std::uint32_t height)
{
    RECT rectangle{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(impl_->handle_, GWL_STYLE));
    AdjustWindowRect(&rectangle, style, FALSE);
    SetWindowPos(
        impl_->handle_,
        nullptr,
        0,
        0,
        rectangle.right - rectangle.left,
        rectangle.bottom - rectangle.top,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

bool Window::ShouldClose() const noexcept
{
    return impl_->shouldClose_;
}

bool Window::Minimized() const noexcept
{
    return impl_->minimized_;
}

std::uint32_t Window::Width() const noexcept
{
    return impl_->width_;
}

std::uint32_t Window::Height() const noexcept
{
    return impl_->height_;
}

void* Window::NativeHandle() const noexcept
{
    return impl_->handle_;
}
} // namespace DeepRun::Platform
