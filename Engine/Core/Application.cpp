#include "Engine/Core/Application.h"

#include "Engine/Audio/AudioEngine.h"
#include "Engine/Core/CoreServices.h"
#include "Engine/Core/Time.h"
#include "Engine/Diagnostics/DebugOverlay.h"
#include "Engine/Input/InputSystem.h"
#include "Engine/Physics/PhysicsWorld.h"
#include "Engine/Platform/Platform.h"
#include "Engine/Platform/Window.h"
#include "Engine/Render/D3D12Renderer.h"

#include <exception>
#include <sstream>

namespace DeepRun::Core
{
namespace
{
constexpr float FixedStepSeconds = 1.0F / 60.0F;
}

ApplicationOptions ApplicationOptions::Parse(const std::span<const std::string_view> arguments)
{
    ApplicationOptions options;
    for (const std::string_view argument : arguments)
    {
        if (argument == "--headless")
        {
            options.headless = true;
        }
        else if (argument == "--smoke-test")
        {
            options.smokeTest = true;
        }
    }
    return options;
}

Application::Application(const ApplicationOptions options)
    : options_(options)
{
}

int Application::Run()
{
    return options_.headless ? RunHeadless() : RunWindowed();
}

int Application::RunHeadless()
{
    CoreServices core;
    Diagnostics::Logger& logger = core.Log();
    logger.Info(Diagnostics::LogCategory::Core, "Headless mode selected; window, rendering, input, ImGui, and audio are skipped");

    Physics::PhysicsWorld physics(logger);
    if (!physics.Initialize())
    {
        logger.Error(Diagnostics::LogCategory::Core, "Headless initialization failed");
        return 2;
    }
    if (!physics.RunGravitySmokeTest())
    {
        logger.Error(Diagnostics::LogCategory::Core, "Headless physics smoke test failed");
        return 3;
    }

    logger.Info(Diagnostics::LogCategory::Core, "Headless smoke test completed successfully");
    return 0;
}

int Application::RunWindowed()
{
    CoreServices core;
    Diagnostics::Logger& logger = core.Log();

    try
    {
        logger.Info(Diagnostics::LogCategory::Platform, "Executable: " + Platform::ExecutablePath().string());

        Physics::PhysicsWorld physics(logger);
        if (!physics.Initialize())
        {
            return 2;
        }

        Audio::AudioEngine audio(logger);
        const bool audioReady = audio.Initialize();

        Platform::Window window(logger, {});
        Input::InputSystem input(logger);

        Render::D3D12Renderer renderer(logger);
        if (!renderer.Initialize(window.NativeHandle(), window.Width(), window.Height()))
        {
            return 4;
        }

        Diagnostics::DebugOverlay debugOverlay(logger);
        if (!debugOverlay.Initialize(window, renderer))
        {
            return 5;
        }

        FrameTimer timer;
        double fixedAccumulator = 0.0;
        std::uint64_t frameNumber = 0;

        while (!window.ShouldClose())
        {
            input.BeginFrame();
            const std::span<const Platform::WindowEvent> events = window.PumpEvents();
            input.ProcessEvents(events);
            input.UpdateController();

            for (const Platform::WindowEvent& event : events)
            {
                if (event.type == Platform::WindowEventType::Resized && event.width > 0 && event.height > 0)
                {
                    renderer.Resize(event.width, event.height);
                }
            }

            if (input.WasPressed(Input::InputAction::Quit))
            {
                window.RequestClose();
            }
            if (input.WasPressed(Input::InputAction::ToggleDebugUi))
            {
                debugOverlay.Toggle();
            }
            if (window.ShouldClose())
            {
                break;
            }
            if (window.Minimized())
            {
                window.WaitForEvents();
                timer.Tick();
                continue;
            }

            timer.Tick();
            fixedAccumulator += timer.DeltaSeconds();
            while (fixedAccumulator >= FixedStepSeconds)
            {
                physics.Step(FixedStepSeconds);
                fixedAccumulator -= FixedStepSeconds;
            }

            debugOverlay.BeginFrame();
            debugOverlay.Draw({
                .framesPerSecond = timer.FramesPerSecond(),
                .frameMilliseconds = timer.FrameMilliseconds(),
                .rendererReady = renderer.IsInitialized(),
                .physicsReady = physics.IsInitialized(),
                .audioReady = audioReady,
                .controllerConnected = input.IsControllerConnected()});

            renderer.BeginFrame();
            debugOverlay.Render(renderer.CommandList());
            renderer.EndFrame();

            ++frameNumber;
            if (options_.smokeTest && frameNumber == 30)
            {
                window.SetClientSize(1024, 640);
            }
            if (options_.smokeTest && frameNumber >= 120)
            {
                window.RequestClose();
            }
        }

        logger.Info(
            Diagnostics::LogCategory::Core,
            options_.smokeTest ? "Windowed rendering and resize smoke test completed" : "Application exit requested");
        return 0;
    }
    catch (const std::exception& exception)
    {
        logger.Error(Diagnostics::LogCategory::Core, exception.what());
        return 1;
    }
}
}
