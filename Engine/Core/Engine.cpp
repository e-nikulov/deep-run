#include "Engine/Core/Engine.h"

#include "Engine/Assets/AssetManager.h"
#include "Engine/Audio/AudioEngine.h"
#include "Engine/Core/CoreServices.h"
#include "Engine/Core/EngineConfig.h"
#include "Engine/Core/FixedStepAccumulator.h"
#include "Engine/Diagnostics/DebugOverlay.h"
#include "Engine/Input/HapticMixer.h"
#include "Engine/Input/InputSystem.h"
#include "Engine/Physics/PhysicsWorld.h"
#include "Engine/Platform/Window.h"
#include "Engine/Render/D3D12Renderer.h"
#include "Engine/Scene/Scene.h"

#include <exception>
#include <memory>
#include <span>
#include <sstream>
#include <utility>

namespace DeepRun::Core
{
class Engine::Impl final
{
public:
    explicit Impl(EngineOptions engineOptions)
        : options(std::move(engineOptions)), assets(options.contentRoot)
    {
    }

    bool Initialize()
    {
        if (lifecycle != EngineLifecycle::Stopped)
        {
            core.Log().Error(Diagnostics::LogCategory::Core, "Engine can only initialize from the stopped state");
            return false;
        }

        exitCode = 0;
        fixedStepAccumulator.Reset();
        simulationTimeSeconds = 0.0;
        hapticMixer.Reset();
        hapticFailureLogged = false;
        audioReady = false;
        lifecycle = EngineLifecycle::Initializing;
        try
        {
            const auto configResult = LoadEngineConfig(options.configPath);
            if (!configResult)
            {
                std::ostringstream message;
                message << "Configuration error in " << configResult.error().path.string() << ": "
                        << configResult.error().message;
                core.Log().Error(Diagnostics::LogCategory::Core, message.str());
                exitCode = 6;
                lifecycle = EngineLifecycle::Stopped;
                return false;
            }
            config = *configResult;
            if (options.performanceRun)
            {
                config.renderer.width = 2560;
                config.renderer.height = 1440;
                config.renderer.hdr = options.performanceHdr;
            }
            if (options.performanceRun)
            {
                core.Log().Info(Diagnostics::LogCategory::Core,
                    config.renderer.vsync ? "Performance run: Present sync interval 1 (configured VSync)"
                                          : "Performance run: Present sync interval 0 (configured VSync off)");
            }
            fixedStepAccumulator.SetStepSeconds(1.0 / static_cast<double>(config.physics.fixedHz));

            physics = std::make_unique<Physics::PhysicsWorld>(core.Log());
            if (!physics->Initialize())
            {
                exitCode = 2;
                ShutdownSubsystems();
                lifecycle = EngineLifecycle::Stopped;
                return false;
            }

            if (!options.headless)
            {
                audio = std::make_unique<Audio::AudioEngine>(core.Log());
                audioReady = audio->Initialize();

                window = std::make_unique<Platform::Window>(
                    core.Log(),
                    Platform::WindowConfig{
                        .title = "DeepRun Engine",
                        .width = config.renderer.width,
                        .height = config.renderer.height,
                        .borderless = options.performanceRun});
                input = std::make_unique<Input::InputSystem>(core.Log(), true, window->NativeHandle());

                renderer = std::make_unique<Render::D3D12Renderer>(core.Log());
                if (!renderer->Initialize(
                        window->NativeHandle(),
                        window->Width(),
                        window->Height(),
                        config.renderer.vsync,
                        config.renderer.hdr,
                        options.shaderRoot))
                {
                    exitCode = 4;
                    ShutdownSubsystems();
                    lifecycle = EngineLifecycle::Stopped;
                    return false;
                }

                debugOverlay = std::make_unique<Diagnostics::DebugOverlay>(core.Log());
                if (!debugOverlay->Initialize(*window, *renderer))
                {
                    exitCode = 5;
                    ShutdownSubsystems();
                    lifecycle = EngineLifecycle::Stopped;
                    return false;
                }
            }

            timer.Reset();
            lifecycle = EngineLifecycle::Running;
            core.Log().Info(
                Diagnostics::LogCategory::Core,
                options.headless ? "Engine initialized in headless mode" : "Engine initialized in windowed mode");
            return true;
        }
        catch (const std::exception& exception)
        {
            core.Log().Error(Diagnostics::LogCategory::Core, exception.what());
            exitCode = 1;
            ShutdownSubsystems();
            lifecycle = EngineLifecycle::Stopped;
            return false;
        }
    }

    bool Update(const Engine::FixedUpdateHook& fixedUpdateHook)
    {
        frameDiagnostics = {};
        if (lifecycle != EngineLifecycle::Running)
        {
            return false;
        }

        if (options.headless)
        {
            timer.Advance(fixedStepAccumulator.StepSeconds());
            // An explicitly supplied hook gives headless behavioral tests one real fixed tick through the
            // same hook-before-physics ordering as the windowed loop. Application deliberately suppresses
            // its gameplay hook in ordinary --headless mode, preserving the established M0 gravity smoke.
            if (fixedUpdateHook)
            {
                static_cast<void>(RunFixedStep(fixedUpdateHook));
                RequestShutdown();
                return false;
            }
            if (!physics->RunGravitySmokeTest())
            {
                core.Log().Error(Diagnostics::LogCategory::Core, "Headless physics smoke test failed");
                exitCode = 3;
            }
            else
            {
                core.Log().Info(Diagnostics::LogCategory::Core, "Headless smoke test completed successfully");
            }
            RequestShutdown();
            return false;
        }

        input->BeginFrame();
        const std::span<const Platform::WindowEvent> events = window->PumpEvents();
        input->ProcessEvents(events);
        input->UpdateController();

        for (const Platform::WindowEvent& event : events)
        {
            if (event.type == Platform::WindowEventType::Resized && event.width > 0 && event.height > 0)
            {
                renderer->Resize(event.width, event.height);
            }
        }

        if (input->WasPressed(Input::InputAction::Quit))
        {
            RequestShutdown();
        }
        if (input->WasPressed(Input::InputAction::ToggleDebugUi))
        {
            debugOverlay->Toggle();
        }
        if (window->ShouldClose())
        {
            RequestShutdown();
        }
        if (lifecycle == EngineLifecycle::ShutdownRequested)
        {
            return false;
        }
        if (window->Minimized())
        {
            if (options.performanceRun)
            {
                core.Log().Error(Diagnostics::LogCategory::Core, "Performance run interrupted by minimization");
                exitCode = 12;
                RequestShutdown();
                return false;
            }
            // Ordinary frame processing is about to suspend in WaitForEvents. Silence the normalized
            // backend output first so the last native motor state cannot remain active while mixer time and
            // simulation are paused. Active generic effects remain owned by the mixer and are not cleared.
            static_cast<void>(input->ApplyGamepadVibration({}));
            window->WaitForEvents();
            timer.Rebase();
            return false;
        }

        timer.Tick();

        // Age only effects inherited from the previous application frame. Current fixed ticks run below and
        // may replace/refresh effects at their full requested lifetime before this frame's backend output.
        // This presentation-time operation runs exactly once regardless of the number of fixed steps.
        const auto hapticsAdvanced = hapticMixer.Advance(static_cast<float>(timer.DeltaSeconds()));
        if (!hapticsAdvanced)
        {
            if (!hapticFailureLogged)
            {
                hapticFailureLogged = true;
                core.Log().Warning(Diagnostics::LogCategory::Input, hapticsAdvanced.error());
            }
            hapticMixer.Reset();
        }

        const std::uint32_t fixedSteps = fixedStepAccumulator.Accumulate(timer.DeltaSeconds());
        const float fixedDeltaSeconds = static_cast<float>(fixedStepAccumulator.StepSeconds());
        const auto fixedStart = std::chrono::steady_clock::now();
        for (std::uint32_t step = 0; step < fixedSteps; ++step)
        {
            if (!RunFixedStep(fixedUpdateHook, fixedDeltaSeconds))
            {
                return false;
            }
        }

        frameDiagnostics.fixedMilliseconds =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - fixedStart).count();

        // A disconnected controller is normal and ApplyGamepadVibration degrades to silence. Backend status
        // is presentation-only and must never fail an otherwise successful frame or fixed update. Requests
        // submitted by current fixed ticks are resolved here without retroactive ageing by this frame's delta.
        static_cast<void>(input->ApplyGamepadVibration(hapticMixer.CurrentOutput()));

        if (options.smokeTest && timer.FrameIndex() == 30)
        {
            window->SetClientSize(1024, 640);
        }
        // Keep the historical 120-frame resize/render floor, but do not let a fast CI runner terminate before
        // the kilometer-scale M5 conventional-torpedo profile can complete its real fixed-step/Jolt impact path.
        constexpr double m5SmokeMinimumSimulationTimeSeconds = 40.0;
        if (options.smokeTest && timer.FrameIndex() >= 120 &&
            simulationTimeSeconds >= m5SmokeMinimumSimulationTimeSeconds)
        {
            RequestShutdown();
            return false;
        }
        if (options.resizeStress && resizeStage < 4U && timer.ElapsedSeconds() >= resizeStage + 1.0)
        {
            ++resizeStage;
            window->SetClientSize(resizeStage % 2U == 0U ? 2560U : 1024U,
                                  resizeStage % 2U == 0U ? 1440U : 640U);
        }
        return true;
    }

    bool RunFixedStep(
        const Engine::FixedUpdateHook& fixedUpdateHook,
        const float fixedDeltaSeconds = 0.0F)
    {
        const float stepSeconds = fixedDeltaSeconds > 0.0F
                                      ? fixedDeltaSeconds
                                      : static_cast<float>(fixedStepAccumulator.StepSeconds());
        if (fixedUpdateHook && !fixedUpdateHook(stepSeconds))
        {
            core.Log().Error(Diagnostics::LogCategory::Core, "Game fixed-update hook failed");
            exitCode = 11;
            RequestShutdown();
            return false;
        }

        // Force producers run immediately before the exact physics step that consumes their transient
        // forces. A failed hook returns above, so that fixed tick is never integrated.
        physics->Step(stepSeconds);
        simulationTimeSeconds += static_cast<double>(stepSeconds);
        return true;
    }

    bool Render(const Engine::RenderHook& renderHook)
    {
        if (lifecycle != EngineLifecycle::Running || options.headless)
        {
            return false;
        }

        debugOverlay->BeginFrame();
        debugOverlay->Draw({
            .framesPerSecond = timer.FramesPerSecond(),
            .frameMilliseconds = timer.FrameMilliseconds(),
            .elapsedSeconds = timer.ElapsedSeconds(),
            .frameIndex = timer.FrameIndex(),
            .entityCount = scene.EntityCount(),
            .resourceCount = assets.CachedResourceCount(),
            .rendererReady = renderer->IsInitialized(),
            .physicsReady = physics->IsInitialized(),
            .audioReady = audioReady,
            .controllerConnected = input->IsControllerConnected()});

        renderer->BeginFrame();
        // The renderer receives the existing elapsed frame clock as PresentationTime. This is deliberately
        // outside the fixed-step loop, so M3-D visual motion cannot advance or influence SimulationTime.
        renderer->SetPresentationTime(static_cast<float>(timer.ElapsedSeconds()));
        const auto submissionStart = std::chrono::steady_clock::now();
        const bool gameRenderSucceeded = !renderHook || renderHook(*renderer);
        frameDiagnostics.gameSubmissionMilliseconds =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - submissionStart).count();
        renderer->OutputSceneToDisplay();
        debugOverlay->Render(renderer->CommandList());
        renderer->EndFrame();
        if (!gameRenderSucceeded)
        {
            core.Log().Error(Diagnostics::LogCategory::Render, "Game render hook failed");
            exitCode = 10;
            RequestShutdown();
        }
        return gameRenderSucceeded;
    }

    void RequestShutdown() noexcept
    {
        if (lifecycle == EngineLifecycle::Running)
        {
            lifecycle = EngineLifecycle::ShutdownRequested;
        }
    }

    void Shutdown() noexcept
    {
        if (lifecycle == EngineLifecycle::Stopped)
        {
            return;
        }

        lifecycle = EngineLifecycle::ShutdownRequested;
        core.Log().Info(
            Diagnostics::LogCategory::Core,
            options.smokeTest && !options.headless
                ? "Windowed rendering and resize smoke test completed"
                : "Engine shutdown requested");
        ShutdownSubsystems();
        lifecycle = EngineLifecycle::Stopped;
        core.Log().Info(Diagnostics::LogCategory::Core, "Engine shut down");
    }

    void ShutdownSubsystems() noexcept
    {
        if (renderer != nullptr)
        {
            try
            {
                renderer->WaitForIdle();
            }
            catch (const std::exception& exception)
            {
                core.Log().Error(Diagnostics::LogCategory::Render, exception.what());
            }
        }
        debugOverlay.reset();
        renderer.reset();

        core.Log().Info(Diagnostics::LogCategory::Core, "Clearing active scene before dependent services");
        scene.Clear();
        core.Log().Info(Diagnostics::LogCategory::Assets, "Clearing manager-owned asset cache after scene");
        assets.Clear();

        hapticMixer.Reset();
        if (input != nullptr)
        {
            static_cast<void>(input->ApplyGamepadVibration({}));
        }
        input.reset();
        window.reset();
        audio.reset();
        physics.reset();
    }

    EngineOptions options;
    CoreServices core;
    EngineConfig config;
    FrameTimer timer;
    CpuFrameDiagnostics frameDiagnostics;
    std::uint32_t resizeStage = 0;
    Scene::Scene scene;
    Assets::AssetManager assets;
    std::unique_ptr<Physics::PhysicsWorld> physics;
    std::unique_ptr<Audio::AudioEngine> audio;
    std::unique_ptr<Platform::Window> window;
    std::unique_ptr<Input::InputSystem> input;
    std::unique_ptr<Render::D3D12Renderer> renderer;
    std::unique_ptr<Diagnostics::DebugOverlay> debugOverlay;
    EngineLifecycle lifecycle = EngineLifecycle::Stopped;
    FixedStepAccumulator fixedStepAccumulator;
    double simulationTimeSeconds = 0.0;
    Input::HapticMixer hapticMixer;
    int exitCode = 0;
    bool audioReady = false;
    bool hapticFailureLogged = false;
};

Engine::Engine(EngineOptions options)
    : impl_(std::make_unique<Impl>(std::move(options)))
{
}

Engine::~Engine()
{
    impl_->Shutdown();
}

bool Engine::Initialize()
{
    return impl_->Initialize();
}

bool Engine::Update(const FixedUpdateHook& fixedUpdateHook)
{
    return impl_->Update(fixedUpdateHook);
}

bool Engine::Render(const RenderHook& renderHook)
{
    return impl_->Render(renderHook);
}

void Engine::RequestShutdown() noexcept
{
    impl_->RequestShutdown();
}

void Engine::Shutdown() noexcept
{
    impl_->Shutdown();
}

EngineLifecycle Engine::Lifecycle() const noexcept
{
    return impl_->lifecycle;
}

const FrameState& Engine::CurrentFrame() const noexcept
{
    return impl_->timer.State();
}

const CpuFrameDiagnostics& Engine::FrameDiagnostics() const noexcept
{
    return impl_->frameDiagnostics;
}

double Engine::SimulationTimeSeconds() const noexcept
{
    return impl_->simulationTimeSeconds;
}

Scene::Scene& Engine::ActiveScene() noexcept
{
    return impl_->scene;
}

Assets::AssetManager& Engine::Assets() noexcept
{
    return impl_->assets;
}

Render::D3D12Renderer* Engine::Renderer() noexcept
{
    return impl_->renderer.get();
}

Physics::PhysicsWorld* Engine::Physics() noexcept
{
    return impl_->physics.get();
}

const Input::InputState* Engine::InputState() const noexcept
{
    return impl_->input != nullptr ? &impl_->input->State() : nullptr;
}

std::expected<void, std::string> Engine::SubmitHapticEffect(const Input::HapticEffectRequest& request)
{
    return impl_->hapticMixer.Submit(request);
}

int Engine::ExitCode() const noexcept
{
    return impl_->exitCode;
}
