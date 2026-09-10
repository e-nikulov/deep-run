#pragma once

#include "Engine/Diagnostics/Logger.h"
#include "Engine/Input/InputSystem.h"
#include "Game/Camera/MultiScaleTacticalCamera.h"
#include "Game/Combat/CalmLaunchCameraAssist.h"

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

    // H.3 extends the normal-play presentation below the historical 600 m local reference so the production
    // Antey can be inspected in detail. Positive wheel steps zoom inward logarithmically and settle smoothly.
    auto close = camera.Update({.wheelSteps = 20.0F}, 1.0 / 60.0);
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
        close->presentationTier != MultiScalePresentationTier::FullDetail ||
        close->targetOffsetXMeters != 0.0F || close->targetOffsetYMeters != 0.0F)
    {
        return false;
    }

    // At 80 m the camera may pan along the production hull, but close inspection is bounded and remains
    // presentation-only. Releasing the stick must not introduce any autonomous chase.
    for (int frame = 0; frame < 240; ++frame)
    {
        close = camera.Update({.panX = 1.0F}, 1.0 / 60.0);
        if (!close)
        {
            return false;
        }
    }
    if (close->targetOffsetXMeters < MultiScaleCloseInspectionMaximumOffsetMeters - 0.1F ||
        close->targetOffsetXMeters > MultiScaleCloseInspectionMaximumOffsetMeters + 0.1F ||
        close->targetOffsetYMeters != 0.0F)
    {
        return false;
    }
    const float closeStableX = close->targetOffsetXMeters;
    for (int frame = 0; frame < 60; ++frame)
    {
        close = camera.Update({}, 1.0 / 60.0);
        if (!close || close->targetOffsetXMeters != closeStableX)
        {
            return false;
        }
    }
    camera.FocusOwnship();
    if (camera.Framing().targetOffsetXMeters != 0.0F)
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
    // remains Operational until the distinct 120 km upper threshold is crossed.
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

    // Horizontal pan is explicit player presentation input only. Vertical semantic input is deliberately
    // reserved and must not alter H.3 framing. Once released, the camera target must not chase contacts.
    const auto panned = camera.Update({.panX = 1.0F, .panY = -1.0F}, 1.0 / 60.0);
    if (!panned || panned->targetOffsetXMeters <= 0.0F || panned->targetOffsetYMeters != 0.0F)
    {
        return false;
    }
    const float stableX = panned->targetOffsetXMeters;
    for (int frame = 0; frame < 120; ++frame)
    {
        const auto stable = camera.Update({}, 1.0 / 60.0);
        if (!stable || stable->targetOffsetXMeters != stableX || stable->targetOffsetYMeters != 0.0F)
        {
            return false;
        }
    }

    // Direct vertical focus remains outside this bounded slice. Horizontal focus is clamped to a bounded
    // close-inspection corridor or the wider ownship-anchored tactical range, whichever is larger.
    if (camera.FocusAtOffsets(1'000.0F, 1.0F) || !camera.FocusAtOffsets(1.0e9F, 0.0F))
    {
        return false;
    }
    const auto clampedWide = camera.Framing();
    const float maximumWideOffset = (std::max)(
        MultiScaleCloseInspectionMaximumOffsetMeters,
        0.5F * (clampedWide.horizontalSpanMeters - MultiScaleLocalReferenceHorizontalSpanMeters));
    if (std::abs(clampedWide.targetOffsetXMeters - maximumWideOffset) > 1.0F ||
        clampedWide.targetOffsetYMeters != 0.0F)
    {
        return false;
    }

    if (!camera.SetRequestedHorizontalSpanMeters(MultiScaleMinimumHorizontalSpanMeters))
    {
        return false;
    }
    camera.FocusOwnship();
    for (int frame = 0; frame < 300; ++frame)
    {
        close = camera.Update({}, 1.0 / 60.0);
        if (!close)
        {
            return false;
        }
    }
    if (std::abs(close->horizontalSpanMeters - MultiScaleMinimumHorizontalSpanMeters) > 0.01F ||
        std::abs(close->targetOffsetXMeters) > 0.01F || close->targetOffsetYMeters != 0.0F)
    {
        return false;
    }

    if (camera.Update({}, -1.0))
    {
        return false;
    }

    // Calm launch framing is one-shot and zoom-only. It does nothing before launch, gently opens a centered
    // frame after launch, never pans, and permanently yields as soon as the player takes manual camera control.
    {
        using Game::Combat::CalmLaunchCameraAssist;
        using Game::Combat::CalmLaunchCameraAssistState;
        MultiScaleTacticalCamera assistedCamera;
        CalmLaunchCameraAssist assist;
        auto manual = MultiScaleCameraInput{};
        auto assistedInput = assist.Update(false, assistedCamera.Framing(), manual);
        if (assist.State() != CalmLaunchCameraAssistState::WaitingForLaunch ||
            assistedInput.zoom != 0.0F || assistedInput.panX != 0.0F)
        {
            return false;
        }

        assistedInput = assist.Update(true, assistedCamera.Framing(), manual);
        if (assist.State() != CalmLaunchCameraAssistState::Transitioning ||
            std::abs(assistedInput.zoom - CalmLaunchCameraAssist::AutoZoomCommand) > 0.001F)
        {
            return false;
        }
        float previousRequestedSpan = assistedCamera.Framing().requestedHorizontalSpanMeters;
        for (int frame = 0; frame < 240 && assist.State() == CalmLaunchCameraAssistState::Transitioning; ++frame)
        {
            const auto updated = assistedCamera.Update(assistedInput, 1.0 / 60.0);
            if (!updated || updated->targetOffsetXMeters != 0.0F ||
                updated->requestedHorizontalSpanMeters < previousRequestedSpan)
            {
                return false;
            }
            previousRequestedSpan = updated->requestedHorizontalSpanMeters;
            assistedInput = assist.Update(true, *updated, manual);
        }
        const auto assistedFraming = assistedCamera.Framing();
        if (assist.State() != CalmLaunchCameraAssistState::Completed ||
            assistedFraming.requestedHorizontalSpanMeters < CalmLaunchCameraAssist::TargetHorizontalSpanMeters * 0.995F ||
            assistedFraming.targetOffsetXMeters != 0.0F || assistedInput.zoom != 0.0F)
        {
            return false;
        }

        CalmLaunchCameraAssist cancelledBeforeLaunch;
        const MultiScaleCameraInput wheelOwned{.wheelSteps = 1.0F};
        const auto unchanged = cancelledBeforeLaunch.Update(false, assistedCamera.Framing(), wheelOwned);
        static_cast<void>(cancelledBeforeLaunch.Update(true, assistedCamera.Framing(), {}));
        if (cancelledBeforeLaunch.State() != CalmLaunchCameraAssistState::CancelledByPlayer ||
            unchanged.wheelSteps != wheelOwned.wheelSteps)
        {
            return false;
        }

        CalmLaunchCameraAssist cancelledDuringTransition;
        static_cast<void>(cancelledDuringTransition.Update(true, MultiScaleTacticalCamera{}.Framing(), {}));
        const MultiScaleCameraInput playerPan{.panX = 0.5F};
        const auto yielded = cancelledDuringTransition.Update(true, MultiScaleTacticalCamera{}.Framing(), playerPan);
        if (cancelledDuringTransition.State() != CalmLaunchCameraAssistState::CancelledByPlayer ||
            yielded.panX != playerPan.panX || yielded.zoom != playerPan.zoom)
        {
            return false;
        }
    }

    // Controller mapping remains semantic before Game consumes it: right stick X pans, right trigger zooms out,
    // left trigger zooms in. The reserved Y axis may exist in Input, but H.3 camera policy above ignores it.
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

    // Synthetic platform events prove keyboard/mouse details terminate inside InputSystem. Game receives only
    // normalized semantic camera axes plus an accumulated wheel-step impulse that resets each frame.
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
