#pragma once

#include "Engine/Diagnostics/Logger.h"
#include "Engine/Input/InputSystem.h"
#include "Game/Camera/MultiScaleTacticalCamera.h"

#include <array>
#include <cmath>

namespace DeepRun::Tests
{
[[nodiscard]] inline bool RunM5MultiScaleCameraChecks()
{
    using namespace Game::Camera;

    MultiScaleTacticalCamera camera;
    const auto initial = camera.Framing();
    if (initial.band != MultiScaleCameraBand::Local ||
        initial.presentationTier != MultiScalePresentationTier::FullDetail ||
        std::abs(initial.horizontalSpanMeters - MultiScaleInitialHorizontalSpanMeters) > 0.001F ||
        initial.targetOffsetXMeters != 0.0F || initial.targetOffsetYMeters != 0.0F)
    {
        return false;
    }

    // Positive mouse-wheel steps zoom inward logarithmically. Repeated presentation updates settle smoothly
    // to the requested close-inspection span without SimulationTime or a gameplay target driving the camera.
    auto close = camera.Update({.wheelSteps = 12.0F}, 1.0 / 60.0);
    if (!close || close->requestedHorizontalSpanMeters != MultiScaleMinimumHorizontalSpanMeters)
    {
        return false;
    }
    for (int frame = 0; frame < 180; ++frame)
    {
        close = camera.Update({}, 1.0 / 60.0);
        if (!close)
        {
            return false;
        }
    }
    if (std::abs(close->horizontalSpanMeters - MultiScaleMinimumHorizontalSpanMeters) > 0.01F ||
        close->band != MultiScaleCameraBand::Detail ||
        close->presentationTier != MultiScalePresentationTier::FullDetail)
    {
        return false;
    }

    if (!camera.SetRequestedHorizontalSpanMeters(MultiScaleMaximumHorizontalSpanMeters))
    {
        return false;
    }
    auto strategic = camera.Update({}, 1.0 / 60.0);
    for (int frame = 0; frame < 240; ++frame)
    {
        strategic = camera.Update({}, 1.0 / 60.0);
        if (!strategic)
        {
            return false;
        }
    }
    if (std::abs(strategic->horizontalSpanMeters - MultiScaleMaximumHorizontalSpanMeters) > 1.0F ||
        strategic->band != MultiScaleCameraBand::Strategic ||
        strategic->presentationTier != MultiScalePresentationTier::StrategicSymbols)
    {
        return false;
    }

    // Strategic -> Operational requires crossing the lower 80 km hysteresis threshold. Returning to 100 km
    // must then remain Operational until the distinct 120 km upper threshold is crossed.
    if (!camera.SetRequestedHorizontalSpanMeters(70'000.0F))
    {
        return false;
    }
    for (int frame = 0; frame < 240; ++frame)
    {
        strategic = camera.Update({}, 1.0 / 60.0);
        if (!strategic)
        {
            return false;
        }
    }
    if (strategic->band != MultiScaleCameraBand::Operational)
    {
        return false;
    }
    if (!camera.SetRequestedHorizontalSpanMeters(100'000.0F))
    {
        return false;
    }
    for (int frame = 0; frame < 180; ++frame)
    {
        strategic = camera.Update({}, 1.0 / 60.0);
        if (!strategic)
        {
            return false;
        }
    }
    if (strategic->band != MultiScaleCameraBand::Operational)
    {
        return false;
    }

    // Pan is explicit player input only. Once released, the camera target must not chase a weapon/contact or
    // drift merely because presentation frames continue advancing.
    const auto panned = camera.Update({.panX = 1.0F, .panY = -0.5F}, 1.0 / 60.0);
    if (!panned || panned->targetOffsetXMeters <= 0.0F || panned->targetOffsetYMeters >= 0.0F)
    {
        return false;
    }
    const float stableX = panned->targetOffsetXMeters;
    const float stableY = panned->targetOffsetYMeters;
    for (int frame = 0; frame < 120; ++frame)
    {
        const auto stable = camera.Update({}, 1.0 / 60.0);
        if (!stable || stable->targetOffsetXMeters != stableX || stable->targetOffsetYMeters != stableY)
        {
            return false;
        }
    }
    camera.FocusOwnship();
    if (camera.Framing().targetOffsetXMeters != 0.0F || camera.Framing().targetOffsetYMeters != 0.0F ||
        camera.Update({}, -1.0))
    {
        return false;
    }

    // Controller mapping is semantic before Game consumes it: right stick pans, right trigger zooms out,
    // left trigger zooms in, while a disconnected controller contributes no camera command.
    const Input::ControllerSemanticAxes controller = Input::SemanticAxesForGamepad(Input::GamepadState{
        .connected = true,
        .rightX = 0.8F,
        .rightY = -0.7F,
        .leftTrigger = 0.1F,
        .rightTrigger = 0.9F});
    if (controller.cameraPanX <= 0.0F || controller.cameraPanY >= 0.0F ||
        std::abs(controller.cameraZoom - 0.8F) > 0.001F)
    {
        return false;
    }
    const auto disconnected = Input::SemanticAxesForGamepad(Input::GamepadState{
        .connected = false,
        .rightX = 1.0F,
        .rightY = 1.0F,
        .rightTrigger = 1.0F});
    if (disconnected.cameraPanX != 0.0F || disconnected.cameraPanY != 0.0F ||
        disconnected.cameraZoom != 0.0F)
    {
        return false;
    }

    // Synthetic platform events prove that keyboard/mouse device details terminate inside InputSystem. Game
    // receives only normalized camera axes plus an accumulated wheel-step impulse that resets each frame.
    Diagnostics::Logger logger;
    Input::InputSystem input(logger, false);
    input.BeginFrame();
    const std::array<Platform::WindowEvent, 3> events{{
        {.type = Platform::WindowEventType::KeyDown, .key = Platform::Key::Right},
        {.type = Platform::WindowEventType::KeyDown, .key = Platform::Key::E},
        {.type = Platform::WindowEventType::MouseWheel, .mouseWheelSteps = 1.5F}}};
    input.ProcessEvents(events);
    if (input.State().Axis(Input::InputAxis::CameraPanX) != 1.0F ||
        input.State().Axis(Input::InputAxis::CameraZoom) != 1.0F ||
        std::abs(input.State().CameraZoomSteps() - 1.5F) > 0.001F)
    {
        return false;
    }
    input.BeginFrame();
    if (input.State().CameraZoomSteps() != 0.0F)
    {
        return false;
    }

    return true;
}
} // namespace DeepRun::Tests
