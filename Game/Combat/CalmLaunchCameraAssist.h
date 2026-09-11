#pragma once

#include "Game/Camera/MultiScaleTacticalCamera.h"

#include <cmath>

namespace DeepRun::Game::Combat
{
enum class CalmLaunchCameraAssistState
{
    WaitingForLaunch,
    Transitioning,
    Completed,
    CancelledByPlayer,
};

class CalmLaunchCameraAssist final
{
public:
    // M5-V1.2 keeps launch assistance local: it may add a little breathing room for the weapon leaving
    // ownship, but it must never reveal the whole kilometre-scale engagement automatically. Target/pre-impact
    // framing is contextual presentation or explicit player navigation, not an all-battlefield zoom.
    static constexpr float TargetHorizontalSpanMeters = 1'000.0F;
    static constexpr float AutoZoomCommand = 0.65F;

    [[nodiscard]] Camera::MultiScaleCameraInput Update(
        const bool weaponLaunched,
        const Camera::MultiScaleCameraFraming& framing,
        const Camera::MultiScaleCameraInput& playerInput) noexcept
    {
        const bool playerNavigating = HasManualNavigation(playerInput);

        if (state_ == CalmLaunchCameraAssistState::WaitingForLaunch)
        {
            if (playerNavigating)
            {
                // A player who has already taken camera control owns framing for this launch. This avoids a
                // later surprise auto-zoom after intentionally inspecting the vessel or choosing a tactical view.
                state_ = CalmLaunchCameraAssistState::CancelledByPlayer;
                return playerInput;
            }
            if (!weaponLaunched)
            {
                return playerInput;
            }
            if (framing.requestedHorizontalSpanMeters >= TargetHorizontalSpanMeters)
            {
                state_ = CalmLaunchCameraAssistState::Completed;
                return playerInput;
            }
            state_ = CalmLaunchCameraAssistState::Transitioning;
        }

        if (state_ == CalmLaunchCameraAssistState::Transitioning)
        {
            if (playerNavigating)
            {
                state_ = CalmLaunchCameraAssistState::CancelledByPlayer;
                return playerInput;
            }
            if (framing.requestedHorizontalSpanMeters >= TargetHorizontalSpanMeters * 0.995F)
            {
                state_ = CalmLaunchCameraAssistState::Completed;
                return playerInput;
            }

            Camera::MultiScaleCameraInput assisted = playerInput;
            assisted.zoom = AutoZoomCommand;
            assisted.panX = 0.0F;
            assisted.panY = 0.0F;
            assisted.wheelSteps = 0.0F;
            return assisted;
        }

        return playerInput;
    }

    [[nodiscard]] CalmLaunchCameraAssistState State() const noexcept
    {
        return state_;
    }

private:
    [[nodiscard]] static bool HasManualNavigation(const Camera::MultiScaleCameraInput& input) noexcept
    {
        constexpr float AnalogEpsilon = 0.01F;
        return std::abs(input.panX) > AnalogEpsilon || std::abs(input.panY) > AnalogEpsilon ||
               std::abs(input.zoom) > AnalogEpsilon || std::abs(input.wheelSteps) > 1.0e-4F;
    }

    CalmLaunchCameraAssistState state_ = CalmLaunchCameraAssistState::WaitingForLaunch;
};
} // namespace DeepRun::Game::Combat
