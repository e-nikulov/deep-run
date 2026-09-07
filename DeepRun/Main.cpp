#include "Engine/Core/Application.h"
#include "Engine/Core/Engine.h"
#include "Engine/Input/InputState.h"
#include "Engine/Physics/PhysicsWorld.h"
#include "Game/Haptics/HapticFeedbackSystem.h"
#include "Game/PhysicalPlayground.h"
#include "Game/Submarine/VesselCommandState.h"

#include <Windows.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
#include <vector>

namespace
{
// The Win32 window class registered by Engine/Platform (WinWindow.cpp). Capture is developer tooling only:
// it finds the game window by this class name and never feeds data back into gameplay or rendering.
constexpr wchar_t GameWindowClassName[] = L"DeepRunEngineWindow";

// EnumWindows callback state for finding THIS process's game window (see WindowFrameCapture::Capture).
struct FindContext final
{
    DWORD processId = 0;
    HWND window = nullptr;
};

BOOL CALLBACK FindGameWindowCallback(HWND handle, LPARAM parameter)
{
    auto* context = reinterpret_cast<FindContext*>(parameter);
    wchar_t className[64]{};
    if (GetClassNameW(handle, className, 63) == 0 || wcscmp(className, GameWindowClassName) != 0)
    {
        return TRUE;
    }
    DWORD windowProcessId = 0;
    GetWindowThreadProcessId(handle, &windowProcessId);
    if (windowProcessId == context->processId)
    {
        context->window = handle;
        return FALSE; // stop enumeration: first match wins
    }
    return TRUE;
}

// Minimal window frame capture for M2 Slice C2 visual validation. It renders the game window into a memory
// bitmap with PrintWindow(PW_RENDERFULLCONTENT) and falls back to a screen BitBlt of the window's client
// rectangle when that yields nothing (e.g. a compositor that does not support off-screen D3D12 swap-chain
// rendering). Any failure degrades to "no screenshot", never a failed run.
class WindowFrameCapture final
{
public:
    // Returns captured 8-bit BGRA pixels plus their dimensions. The window is matched by class name AND the
    // current process ID: FindWindowW alone can return another DeepRun instance's leftover window (e.g. from
    // a previous session), which would capture foreign content at an unrelated size.
    bool Capture(std::vector<std::byte>& bgraPixels, std::uint32_t& width, std::uint32_t& height)
    {
        FindContext findContext{.processId = GetCurrentProcessId(), .window = nullptr};
        const LPARAM contextParameter = reinterpret_cast<LPARAM>(&findContext);
        EnumWindows(FindGameWindowCallback, contextParameter);
        const HWND window = findContext.window;
        if (window == nullptr || !IsWindowVisible(window))
        {
            LogOnce("game window not found for this process");
            return false;
        }

        RECT client{};
        if (!GetClientRect(window, &client) || client.right <= 0 || client.bottom <= 0)
        {
            LogOnce("empty client rect");
            return false;
        }
        width = static_cast<std::uint32_t>(client.right);
        height = static_cast<std::uint32_t>(client.bottom);

        HDC windowDc = GetDC(window);
        if (windowDc == nullptr)
        {
            LogOnce("GetDC failed");
            return false;
        }

        bool captured = false;
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = static_cast<LONG>(width);
        info.bmiHeader.biHeight = -static_cast<LONG>(height); // top-down 32-bit BGRA
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;

        void* bits = nullptr;
        bool haveBits = false;
        HDC memoryDc = CreateCompatibleDC(windowDc);
        if (memoryDc != nullptr)
        {
            HBITMAP dib = CreateDIBSection(windowDc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
            if (dib != nullptr && bits != nullptr)
            {
                haveBits = true;
                const HGDIOBJ previous = SelectObject(memoryDc, dib);
                // PW_RENDERFULLCONTENT asks the window to render its content even when not visible on a
                // composited desktop; without it D3D swap-chain windows frequently come back black.
                if (PrintWindow(window, memoryDc, PW_RENDERFULLCONTENT) && !IsAllBlack(bits, width, height))
                {
                    captured = true;
                }
                else
                {
                    // Fallback: copy whatever the desktop compositor shows at the window's client rect.
                    POINT origin{0, 0};
                    if (ClientToScreen(window, &origin))
                    {
                        HDC screenDc = GetDC(nullptr);
                        if (screenDc != nullptr)
                        {
                            const BOOL copied = BitBlt(
                                memoryDc, 0, 0, width, height, screenDc, origin.x, origin.y, SRCCOPY);
                            ReleaseDC(window, screenDc);
                            captured = copied && !IsAllBlack(bits, width, height);
                        }
                    }
                }
                if (captured)
                {
                    // Copy out while the DIB section is still alive.
                    bgraPixels.resize(static_cast<std::size_t>(width) * height * 4);
                    std::memcpy(bgraPixels.data(), bits, bgraPixels.size());
                }
                SelectObject(memoryDc, previous);
                DeleteObject(dib);
            }
        }
        if (memoryDc != nullptr)
        {
            DeleteDC(memoryDc);
        }
        ReleaseDC(window, windowDc);

        if (!captured || !haveBits)
        {
            LogOnce("PrintWindow and screen BitBlt both produced no usable frame");
            return false;
        }
        return true;
    }

private:
    // One diagnostic line per process so a blocked capture is visible in the smoke log without spamming.
    void LogOnce(const std::string_view reason)
    {
        if (!logged_)
        {
            logged_ = true;
            std::cerr << "[Game][WARN] Frame capture unavailable: " << reason
                      << " (visual validation degrades to log samples only; the run is not failed)\n";
        }
    }

    // Full scan of the top-down 32-bit BGRA buffer (at most a couple of million pixels, run twice per
    // process); used to reject black captures.
    static bool IsAllBlack(const void* bits, const std::uint32_t width, const std::uint32_t height) noexcept
    {
        const auto* pixels = static_cast<const std::uint8_t*>(bits);
        for (std::uint32_t y = 0; y < height; ++y)
        {
            for (std::uint32_t x = 0; x < width; ++x)
            {
                const std::size_t offset = (static_cast<std::size_t>(y) * width + x) * 4U;
                if (pixels[offset] > 8 || pixels[offset + 1] > 8 || pixels[offset + 2] > 8)
                {
                    return false;
                }
            }
        }
        return true;
    }

    bool logged_ = false;
};

bool WriteBmp(
    const std::filesystem::path& path,
    const std::vector<std::byte>& bgraPixels,
    const std::uint32_t width,
    const std::uint32_t height)
{
    if (width == 0 || height == 0 || bgraPixels.size() < static_cast<std::size_t>(width) * height * 4)
    {
        return false;
    }

    // 24-bit BGR, bottom-up rows padded to 4 bytes.
    const std::uint32_t rowBytes = width * 3;
    const std::uint32_t paddedRow = (rowBytes + 3U) & ~3U;
    const std::uint32_t pixelDataSize = paddedRow * height;

    std::vector<std::byte> file(14 + 40 + pixelDataSize, std::byte{0});
    auto put32 = [&file](const std::size_t offset, const std::uint32_t value) {
        file[offset] = static_cast<std::byte>(value & 0xFFU);
        file[offset + 1] = static_cast<std::byte>((value >> 8U) & 0xFFU);
        file[offset + 2] = static_cast<std::byte>((value >> 16U) & 0xFFU);
        file[offset + 3] = static_cast<std::byte>((value >> 24U) & 0xFFU);
    };
    auto put16 = [&file](const std::size_t offset, const std::uint16_t value) {
        file[offset] = static_cast<std::byte>(value & 0xFFU);
        file[offset + 1] = static_cast<std::byte>((value >> 8U) & 0xFFU);
    };

    put32(0, 0x4D42U); // "BM"
    put32(2, static_cast<std::uint32_t>(file.size()));
    put32(10, 14 + 40);
    put32(14, 40);
    put32(18, width);
    put32(22, height);
    put16(26, 1);
    put16(28, 24);

    std::byte* data = file.data() + 54;
    for (std::uint32_t y = 0; y < height; ++y)
    {
        const std::uint32_t sourceRow = height - 1U - y; // bottom-up
        std::byte* targetRow = data + static_cast<std::size_t>(y) * paddedRow;
        for (std::uint32_t x = 0; x < width; ++x)
        {
            const std::size_t sourceOffset = (static_cast<std::size_t>(sourceRow) * width + x) * 4;
            // The capture buffer is a top-down 32-bit BGRA DIB section: byte order in memory is B,G,R,A.
            // A 24-bit BMP row stores B,G,R per pixel, so the channels map straight through (no swap).
            targetRow[x * 3 + 0] = bgraPixels[sourceOffset + 0]; // B
            targetRow[x * 3 + 1] = bgraPixels[sourceOffset + 1]; // G
            targetRow[x * 3 + 2] = bgraPixels[sourceOffset + 2]; // R
        }
    }

    std::ofstream output(path, std::ios::binary);
    if (!output)
    {
        return false;
    }
    output.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
    return static_cast<bool>(output);
}
} // namespace

int main(const int argumentCount, char** argumentValues)
{
    try
    {
        std::vector<std::string_view> arguments;
        arguments.reserve(static_cast<std::size_t>(argumentCount > 1 ? argumentCount - 1 : 0));
        for (int index = 1; index < argumentCount; ++index)
        {
            arguments.emplace_back(argumentValues[index]);
        }

        const DeepRun::Core::ApplicationOptions options = DeepRun::Core::ApplicationOptions::Parse(arguments);
        DeepRun::Game::PhysicalPlayground playground;
        DeepRun::Game::HapticFeedbackSystem hapticFeedback;
        WindowFrameCapture frameCapture;
        std::uint64_t renderFrames = 0;
        bool capturedInitial = false;
        bool capturedLater = false;
        bool loggedHapticSubmissionFailure = false;
        DeepRun::Core::Engine* engineServices = nullptr;
        const DeepRun::Input::InputState* inputState = nullptr;
        // Bisection aid: DR_NO_CAPTURE=1 disables window frame capture entirely so the physics/render path
        // can be tested in isolation. _dupenv_s allocates with malloc (not new), so the pointer must be
        // released with std::free.
        char* noCaptureValue = nullptr;
        std::size_t noCaptureSize = 0;
        const bool captureDisabledByEnvironment =
            _dupenv_s(&noCaptureValue, &noCaptureSize, "DR_NO_CAPTURE") == 0 && noCaptureValue != nullptr;
        const bool captureEnabled = !options.benchmarkM3 && !captureDisabledByEnvironment;
        std::free(noCaptureValue);

        DeepRun::Core::Application application(
            options,
            [&options, &playground, &inputState, &engineServices](DeepRun::Core::Engine& engine)
            {
                engineServices = &engine;
                if (options.headless)
                {
                    return true;
                }

                DeepRun::Render::D3D12Renderer* renderer = engine.Renderer();
                if (renderer == nullptr)
                {
                    std::cerr << "[Game][ERROR] Physical playground requires a windowed renderer\n";
                    return false;
                }

                // The Engine owns the PhysicsWorld and outlives all playground rendering during Run.
                DeepRun::Physics::PhysicsWorld* physics = engine.Physics();
                if (physics == nullptr || !physics->IsInitialized())
                {
                    std::cerr << "[Game][ERROR] Physical playground requires an initialized physics world\n";
                    return false;
                }

                inputState = engine.InputState();
                if (inputState == nullptr)
                {
                    std::cerr << "[Game][ERROR] Physical playground requires windowed input state\n";
                    return false;
                }

                const auto initialized =
                    playground.Initialize(engine.Assets(), *physics, *renderer, options.smokeTest);
                if (!initialized)
                {
                    std::cerr << "[Game][ERROR] " << initialized.error() << '\n';
                    return false;
                }
                return playground.SubmarineModel().IsValid();
            },
            [&options, &playground, &hapticFeedback, &inputState, &engineServices,
             &loggedHapticSubmissionFailure](const float fixedDeltaSeconds)
            {
                // Smoke runs deliberately consume an explicit neutral command, insulating deterministic
                // automated validation from any live controller connected to the developer machine.
                const auto command = (options.smokeTest || options.benchmarkM3)
                                         ? std::expected<DeepRun::Game::VesselCommandState, std::string>{
                                               DeepRun::Game::VesselCommandState{}}
                                         : inputState != nullptr
                                               ? DeepRun::Game::VesselCommandStateFromInput(*inputState)
                                               : std::expected<DeepRun::Game::VesselCommandState, std::string>{
                                                     std::unexpected("windowed input state is unavailable")};
                if (!command)
                {
                    std::cerr << "[Game][ERROR] vessel command mapping failed: " << command.error() << '\n';
                    return false;
                }
                const auto updated = playground.FixedUpdate(
                    fixedDeltaSeconds,
                    engineServices->SimulationTimeSeconds(),
                    *command,
                    [&hapticFeedback, &engineServices, &loggedHapticSubmissionFailure](
                        const DeepRun::Game::HapticEvent& event)
                    {
                        const auto effect = hapticFeedback.Map(event);
                        if (!effect || engineServices == nullptr)
                        {
                            if (!loggedHapticSubmissionFailure)
                            {
                                loggedHapticSubmissionFailure = true;
                                std::cerr << "[Game][WARN] Haptic presentation suppressed: "
                                          << (effect ? "engine haptic service is unavailable" : effect.error())
                                          << '\n';
                            }
                            return;
                        }
                        const auto submitted = engineServices->SubmitHapticEffect(*effect);
                        if (!submitted && !loggedHapticSubmissionFailure)
                        {
                            loggedHapticSubmissionFailure = true;
                            std::cerr << "[Game][WARN] Haptic presentation suppressed: "
                                      << submitted.error() << '\n';
                        }
                    });
                if (!updated)
                {
                    std::cerr << "[Game][ERROR] " << updated.error() << '\n';
                    return false;
                }
                return true;
            },
            [&playground, &frameCapture, &captureEnabled, &options, &renderFrames, &capturedInitial,
             &capturedLater, &engineServices](DeepRun::Render::D3D12Renderer& renderer)
            {
                const auto rendered = playground.Render(
                    renderer,
                    engineServices->SimulationTimeSeconds(),
                    engineServices->CurrentFrame().elapsedSeconds);
                if (!rendered)
                {
                    std::cerr << "[Game][ERROR] " << rendered.error() << '\n';
                    return false;
                }

                // Bounded M3-H.1 visual validation: one frame near the start and one later frame makes the
                // static authored ice/flora, moving presentation school, completed-step surface profile, and
                // actual Jolt float response observable. The smoke run resizes at engine frame 30 from 16:9
                // to 16:10 (1280x720 -> 1024x640), so the later capture also proves the fixed 600 m view is
                // covered through a changed projection. Capture failure never fails the run.
                if (captureEnabled && !options.headless && !capturedInitial && renderFrames == 3)
                {
                    std::vector<std::byte> pixels;
                    std::uint32_t width = 0;
                    std::uint32_t height = 0;
                    if (frameCapture.Capture(pixels, width, height))
                    {
                        const auto path = std::filesystem::path("m3_h1_frame_004.bmp");
                        capturedInitial = WriteBmp(path, pixels, width, height);
                        std::cout << "[Game] Captured initial visual frame to " << path.string() << '\n';
                    }
                }
                if (captureEnabled && !options.headless && !capturedLater && renderFrames == 90)
                {
                    std::vector<std::byte> pixels;
                    std::uint32_t width = 0;
                    std::uint32_t height = 0;
                    if (frameCapture.Capture(pixels, width, height))
                    {
                        const auto path = std::filesystem::path("m3_h1_frame_091.bmp");
                        capturedLater = WriteBmp(path, pixels, width, height);
                        std::cout << "[Game] Captured later visual frame to " << path.string() << '\n';
                    }
                }

                if (options.benchmarkM3 && renderFrames == 0)
                {
                    std::cout << "[BenchmarkScene] draws=" << rendered->drawCalls
                              << " model_primitives=" << rendered->submittedPrimitives
                              << " submitted_indices=" << rendered->submittedIndices
                              << " submitted_triangles=" << rendered->submittedIndices / 3U << '\n';
                }
                ++renderFrames;
                // IG1-B replaces the four-primitive prototype submarine with the actual 66-primitive staged
                // Antey LOD0. Model primitives remain model draws only; the Gerstner surface and particles
                // remain their existing non-model batches.
                return rendered->drawCalls == 74 && rendered->submittedPrimitives == 72 &&
                       rendered->submittedIndices == 364380;
            });
        return application.Run();
    }
    catch (const std::exception& exception)
    {
        std::cerr << "[Core][ERROR] Unhandled startup failure: " << exception.what() << '\n';
        return 1;
    }
}
