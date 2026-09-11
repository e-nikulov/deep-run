#pragma once

#include "Game/Combat/CombatPlaygroundRuntime.h"

#include <algorithm>
#include <cmath>
#include <expected>
#include <optional>
#include <string>

namespace DeepRun::Game::Combat
{
// M5 camera is deliberately cinematic rather than a continuously chasing follow-camera. The launch view is
// stable; after the torpedo has cleared the submarine by a substantial distance the director performs exactly
// one smooth zoom-out to a fixed tactical overview and never oscillates back during that engagement.
//
// M5-V1 keeps this presentation-only contract but widens the mandatory combat compositions enough for the
// production Antey and the authored surface destroyer to coexist in the same human-readable side view. The
// camera still never changes physics, sonar, tracks, weapon guidance, target placement or any other authority.
inline constexpr float M5CombatLocalCameraHorizontalSpanMeters = 3'200.0F;
inline constexpr float M5CombatTacticalCameraHorizontalSpanMeters = 3'600.0F;
inline constexpr float M5CombatCameraTransitionTriggerProgressMeters = 250.0F;
inline constexpr double M5CombatCameraTransitionDurationSeconds = 1.5;

enum class CombatPlaygroundCameraMode
{
    LocalLaunch,
    TransitionToTactical,
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

        if (mode_ == CombatPlaygroundCameraMode::LocalLaunch)
        {
            const auto& torpedo = runtime.PlayerTorpedo();
            const auto& launchPosition = runtime.PlayerTorpedoLaunchPosition();
            if (torpedo && launchPosition)
            {
                const float forwardProgressMeters = torpedo->positionMeters.x - launchPosition->x;
                if (std::isfinite(forwardProgressMeters) &&
                    forwardProgressMeters >= M5CombatCameraTransitionTriggerProgressMeters)
                {
                    mode_ = CombatPlaygroundCameraMode::TransitionToTactical;
                    transitionStartTimeSeconds_ = simulationTimeSeconds;
                }
            }
        }

        if (mode_ == CombatPlaygroundCameraMode::TransitionToTactical)
        {
            const double elapsedSeconds = simulationTimeSeconds - transitionStartTimeSeconds_.value_or(simulationTimeSeconds);
            const float linearProgress = static_cast<float>(std::clamp(
                elapsedSeconds / M5CombatCameraTransitionDurationSeconds, 0.0, 1.0));
            const float smoothProgress = linearProgress * linearProgress * (3.0F - 2.0F * linearProgress);
            if (linearProgress >= 1.0F)
            {
                mode_ = CombatPlaygroundCameraMode::TacticalOverview;
                return TacticalFraming();
            }
            return CombatPlaygroundCameraFraming{
                .mode = CombatPlaygroundCameraMode::TransitionToTactical,
                .targetOffsetXMeters = M5CombatCameraTargetOffsetXMeters,
                .horizontalSpanMeters = M5CombatLocalCameraHorizontalSpanMeters +
                    (M5CombatTacticalCameraHorizontalSpanMeters - M5CombatLocalCameraHorizontalSpanMeters) *
                        smoothProgress,
                .transitionProgress = smoothProgress};
        }

        if (mode_ == CombatPlaygroundCameraMode::TacticalOverview)
        {
            return TacticalFraming();
        }

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
    CombatPlaygroundCameraMode mode_ = CombatPlaygroundCameraMode::LocalLaunch;
    std::optional<double> transitionStartTimeSeconds_{};
    double lastSimulationTimeSeconds_ = 0.0;
};
} // namespace DeepRun::Game::Combat
