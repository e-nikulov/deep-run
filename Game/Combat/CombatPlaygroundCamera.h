#pragma once

#include "Game/Combat/CombatPlaygroundRuntime.h"

#include <cmath>
#include <expected>
#include <string>

namespace DeepRun::Game::Combat
{
// M5-V1.2 restores normal combat to a genuinely local underwater side view. The automatic director never
// zooms out to show the whole engagement: it keeps the accepted 800 m local scale and moves the presentation
// focus from ownship to the live torpedo/impact only after the weapon has cleared the local launch composition.
// The 3.6 km framing remains available only as an explicit tactical overview. This is presentation policy only;
// physics, perception, weapon guidance, target placement and damage authority are unchanged.
inline constexpr float M5CombatLocalCameraHorizontalSpanMeters = 800.0F;
inline constexpr float M5CombatTacticalCameraHorizontalSpanMeters = 3'600.0F;
inline constexpr float M5CombatCameraFollowTriggerProgressMeters = 250.0F;

enum class CombatPlaygroundCameraMode
{
    LocalLaunch,
    TorpedoFollow,
    ImpactFocus,
    TacticalOverview,
};

struct CombatPlaygroundCameraFraming final
{
    CombatPlaygroundCameraMode mode = CombatPlaygroundCameraMode::LocalLaunch;
    float targetOffsetXMeters = M5CombatCameraTargetOffsetXMeters;
    float horizontalSpanMeters = M5CombatLocalCameraHorizontalSpanMeters;
    float transitionProgress = 0.0F;
};

class CombatPlaygroundCameraDirector final
{
public:
    [[nodiscard]] std::expected<CombatPlaygroundCameraFraming, std::string> Evaluate(
        const CombatPlaygroundRuntime& runtime,
        const double simulationTimeSeconds)
    {
        if (!std::isfinite(simulationTimeSeconds) || simulationTimeSeconds < lastSimulationTimeSeconds_)
        {
            return std::unexpected("M5 combat camera received invalid or time-reversing SimulationTime");
        }
        lastSimulationTimeSeconds_ = simulationTimeSeconds;

        const auto& torpedo = runtime.PlayerTorpedo();
        const auto& launchPosition = runtime.PlayerTorpedoLaunchPosition();
        if (!torpedo || !launchPosition)
        {
            mode_ = CombatPlaygroundCameraMode::LocalLaunch;
            return LocalFraming();
        }

        const float ownshipReferenceXMeters = launchPosition->x - M5CombatTorpedoLaunchClearanceMeters;
        const float torpedoOffsetXMeters = torpedo->positionMeters.x - ownshipReferenceXMeters;
        if (!std::isfinite(ownshipReferenceXMeters) || !std::isfinite(torpedoOffsetXMeters))
        {
            return std::unexpected("M5 combat camera received a non-finite contextual focus");
        }

        if (torpedo->movementDomain == Weapons::MovementDomain::Underwater)
        {
            const float forwardProgressMeters = torpedo->positionMeters.x - launchPosition->x;
            if (!std::isfinite(forwardProgressMeters))
            {
                return std::unexpected("M5 combat camera received non-finite torpedo progress");
            }
            if (forwardProgressMeters >= M5CombatCameraFollowTriggerProgressMeters)
            {
                mode_ = CombatPlaygroundCameraMode::TorpedoFollow;
                return ContextualFraming(CombatPlaygroundCameraMode::TorpedoFollow, torpedoOffsetXMeters);
            }
        }
        else if (torpedo->movementDomain == Weapons::MovementDomain::Spent)
        {
            mode_ = CombatPlaygroundCameraMode::ImpactFocus;
            return ContextualFraming(CombatPlaygroundCameraMode::ImpactFocus, torpedoOffsetXMeters);
        }

        mode_ = CombatPlaygroundCameraMode::LocalLaunch;
        return LocalFraming();
    }

    [[nodiscard]] CombatPlaygroundCameraMode Mode() const noexcept
    {
        return mode_;
    }

    [[nodiscard]] static constexpr CombatPlaygroundCameraFraming LocalFraming() noexcept
    {
        return CombatPlaygroundCameraFraming{
            .mode = CombatPlaygroundCameraMode::LocalLaunch,
            .targetOffsetXMeters = M5CombatCameraTargetOffsetXMeters,
            .horizontalSpanMeters = M5CombatLocalCameraHorizontalSpanMeters,
            .transitionProgress = 0.0F};
    }

    [[nodiscard]] static constexpr CombatPlaygroundCameraFraming TacticalFraming() noexcept
    {
        return CombatPlaygroundCameraFraming{
            .mode = CombatPlaygroundCameraMode::TacticalOverview,
            .targetOffsetXMeters = M5CombatCameraTargetOffsetXMeters,
            .horizontalSpanMeters = M5CombatTacticalCameraHorizontalSpanMeters,
            .transitionProgress = 1.0F};
    }

private:
    [[nodiscard]] static constexpr CombatPlaygroundCameraFraming ContextualFraming(
        const CombatPlaygroundCameraMode mode,
        const float targetOffsetXMeters) noexcept
    {
        return CombatPlaygroundCameraFraming{
            .mode = mode,
            .targetOffsetXMeters = targetOffsetXMeters,
            .horizontalSpanMeters = M5CombatLocalCameraHorizontalSpanMeters,
            .transitionProgress = 1.0F};
    }

    CombatPlaygroundCameraMode mode_ = CombatPlaygroundCameraMode::LocalLaunch;
    double lastSimulationTimeSeconds_ = 0.0;
};
} // namespace DeepRun::Game::Combat
