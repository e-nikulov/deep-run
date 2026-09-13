#pragma once

#include "Engine/Physics/PhysicsTypes.h"

#include <algorithm>
#include <cmath>
#include <expected>
#include <string>

namespace DeepRun::Game::Submarine
{
// Game-owned low-speed depth-control policy. It models the *effect* of variable ballast / trim compensation,
// not individual tanks, pumps, valves or classified Project 949A hydrostatics. Diving planes remain the
// hydrodynamic pitch authority and naturally lose lift with forward flow; this controller supplies only a
// bounded net vertical force near zero speed so the submarine can hover, rise and sink without propulsion.
struct VariableBallastDepthControlConfig final
{
    float fullAuthorityBelowForwardSpeedMetersPerSecond = 0.75F;
    float zeroAuthorityAboveForwardSpeedMetersPerSecond = 4.0F;
    float commandedVerticalSpeedMetersPerSecond = 1.0F;
    float maximumForceFractionOfWeight = 0.015F;
};

struct VariableBallastDepthControlResult final
{
    float lowSpeedAuthorityFraction = 0.0F;
    float targetVerticalSpeedMetersPerSecond = 0.0F;
    float forceFractionOfWeight = 0.0F;
    Physics::PhysicsVector3 forceNewtons{};
};

[[nodiscard]] inline std::expected<VariableBallastDepthControlResult, std::string>
CalculateVariableBallastDepthControl(
    const VariableBallastDepthControlConfig& config,
    const float depthCommandFraction,
    const float bodyForwardSpeedMetersPerSecond,
    const float worldVerticalSpeedMetersPerSecond,
    const float vesselWeightNewtons)
{
    if (!std::isfinite(config.fullAuthorityBelowForwardSpeedMetersPerSecond) ||
        !std::isfinite(config.zeroAuthorityAboveForwardSpeedMetersPerSecond) ||
        !std::isfinite(config.commandedVerticalSpeedMetersPerSecond) ||
        !std::isfinite(config.maximumForceFractionOfWeight) ||
        config.fullAuthorityBelowForwardSpeedMetersPerSecond < 0.0F ||
        config.zeroAuthorityAboveForwardSpeedMetersPerSecond <= config.fullAuthorityBelowForwardSpeedMetersPerSecond ||
        config.commandedVerticalSpeedMetersPerSecond <= 0.0F ||
        config.maximumForceFractionOfWeight <= 0.0F || config.maximumForceFractionOfWeight > 0.10F ||
        !std::isfinite(depthCommandFraction) || depthCommandFraction < -1.0F || depthCommandFraction > 1.0F ||
        !std::isfinite(bodyForwardSpeedMetersPerSecond) || !std::isfinite(worldVerticalSpeedMetersPerSecond) ||
        !std::isfinite(vesselWeightNewtons) || vesselWeightNewtons <= 0.0F)
    {
        return std::unexpected("invalid variable-ballast depth-control input");
    }

    const float forwardSpeed = std::abs(bodyForwardSpeedMetersPerSecond);
    float authority = 1.0F;
    if (forwardSpeed >= config.zeroAuthorityAboveForwardSpeedMetersPerSecond)
    {
        authority = 0.0F;
    }
    else if (forwardSpeed > config.fullAuthorityBelowForwardSpeedMetersPerSecond)
    {
        const float span = config.zeroAuthorityAboveForwardSpeedMetersPerSecond -
                           config.fullAuthorityBelowForwardSpeedMetersPerSecond;
        authority = 1.0F -
                    (forwardSpeed - config.fullAuthorityBelowForwardSpeedMetersPerSecond) / span;
    }
    authority = std::clamp(authority, 0.0F, 1.0F);

    // Canonical Depth is -1 surface/nose-up, +1 dive/nose-down. World +Y is upward, therefore the desired
    // vertical velocity is the inverse sign. When the stick returns to neutral, target V/S becomes zero and
    // the same trim authority gently arrests residual vertical motion instead of leaving an endless drift.
    const float targetVerticalSpeed =
        -depthCommandFraction * config.commandedVerticalSpeedMetersPerSecond;
    const float normalizedVelocityError = std::clamp(
        (targetVerticalSpeed - worldVerticalSpeedMetersPerSecond) /
            config.commandedVerticalSpeedMetersPerSecond,
        -1.0F,
        1.0F);
    const float forceFraction =
        normalizedVelocityError * config.maximumForceFractionOfWeight * authority;

    return VariableBallastDepthControlResult{
        .lowSpeedAuthorityFraction = authority,
        .targetVerticalSpeedMetersPerSecond = targetVerticalSpeed,
        .forceFractionOfWeight = forceFraction,
        .forceNewtons = {.x = 0.0F, .y = forceFraction * vesselWeightNewtons, .z = 0.0F}};
}
} // namespace DeepRun::Game::Submarine
