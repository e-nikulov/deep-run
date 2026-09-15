#pragma once

#include "Engine/Render/GerstnerSurface.h"
#include "Game/Weapons/P700LaunchVfx.h"

#include <algorithm>
#include <cmath>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace DeepRun::Game::Armament
{
[[nodiscard]] inline std::expected<std::vector<Render::GerstnerSurfaceTransientDisturbance>, std::string>
BuildP700SurfaceDisturbances(
    const std::span<const Weapons::P700GranitRuntimeState* const> missiles,
    const P700LaunchVfxTuning& tuning,
    const double simulationTimeSeconds)
{
    if (!std::isfinite(simulationTimeSeconds) || simulationTimeSeconds < 0.0)
        return std::unexpected("P-700 surface presentation time must be finite and non-negative");
    if (const auto valid = ValidateP700LaunchVfxTuning(tuning); !valid)
        return std::unexpected(valid.error());

    std::vector<Render::GerstnerSurfaceTransientDisturbance> candidates;
    candidates.reserve(missiles.size());
    constexpr float Pi = 3.14159265358979323846F;
    for (const Weapons::P700GranitRuntimeState* missile : missiles)
    {
        if (missile == nullptr || missile->phase == Weapons::P700GranitPhase::Stored ||
            missile->phase == Weapons::P700GranitPhase::Spent)
            continue;
        if (!missile->positionMeters.IsFinite() || !std::isfinite(missile->surfaceLevelYMeters) ||
            !std::isfinite(missile->phaseStartTimeSeconds) || simulationTimeSeconds < missile->phaseStartTimeSeconds)
            return std::unexpected("P-700 surface presentation observed invalid missile state");

        if (missile->phase == Weapons::P700GranitPhase::UnderwaterLaunch)
        {
            const float depth = missile->surfaceLevelYMeters - missile->positionMeters.y;
            if (depth >= 0.0F && depth < tuning.breachPreReactionDepthMeters)
            {
                const float reaction = std::clamp(1.0F - depth / tuning.breachPreReactionDepthMeters, 0.0F, 1.0F);
                const float smoothReaction = reaction * reaction * (3.0F - 2.0F * reaction);
                candidates.push_back({
                    .centerX = missile->positionMeters.x,
                    .radiusMeters = tuning.preBreachBulgeRadiusMeters * (0.55F + 0.45F * smoothReaction),
                    .verticalAmplitudeMeters = tuning.preBreachBulgeMaximumMeters * smoothReaction * smoothReaction,
                    .profileExponent = tuning.surfaceDisturbanceProfileExponent});
            }
        }
        else if (missile->phase == Weapons::P700GranitPhase::WaterExit)
        {
            const float age = static_cast<float>(simulationTimeSeconds - missile->phaseStartTimeSeconds);
            if (age >= 0.0F && age < tuning.breachSurfaceRelaxationSeconds)
            {
                const float normalizedAge = std::clamp(age / tuning.breachSurfaceRelaxationSeconds, 0.0F, 1.0F);
                const float retainedBulge = tuning.preBreachBulgeMaximumMeters * (1.0F - normalizedAge);
                const float collapsingShoulder = tuning.breachSurfaceCollapseMeters * std::sin(Pi * normalizedAge);
                const float amplitude = retainedBulge - collapsingShoulder;
                if (std::abs(amplitude) > 0.01F)
                {
                    candidates.push_back({
                        .centerX = missile->positionMeters.x,
                        .radiusMeters = tuning.preBreachBulgeRadiusMeters +
                            tuning.breachSurfaceExpansionMetersPerSecond * age,
                        .verticalAmplitudeMeters = amplitude,
                        .profileExponent = tuning.surfaceDisturbanceProfileExponent});
                }
            }
        }
    }

    std::ranges::sort(candidates, [](const auto& left, const auto& right) {
        return std::abs(left.verticalAmplitudeMeters) > std::abs(right.verticalAmplitudeMeters);
    });
    if (candidates.size() > Render::GerstnerSurfaceTransientDisturbanceCapacity)
        candidates.resize(Render::GerstnerSurfaceTransientDisturbanceCapacity);
    if (const auto valid = Render::ValidateGerstnerSurfaceTransientDisturbances(candidates); !valid)
        return std::unexpected("P-700 surface presentation produced invalid disturbances: " + valid.error());
    return candidates;
}
} // namespace DeepRun::Game::Armament
