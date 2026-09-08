#pragma once

#include "Simulation/Weapons/WeaponRuntime.h"

#include <algorithm>
#include <cmath>
#include <expected>
#include <optional>
#include <string>

namespace DeepRun::Weapons
{
enum class MovementDomain
{
    Attached,
    Underwater,
};

struct ConventionalTorpedoDefinition final
{
    WeaponDefinition weapon{};
    float underwaterSpeedMetersPerSecond = 20.0F;
    float maximumTurnRateRadiansPerSecond = 0.25F;
};

// Bounded M5-C runtime: authoritative weapon phase remains in WeaponRuntimeState, while movement owns only
// underwater kinematics and a perceived-world guidance snapshot. No target entity handle or Transform exists here.
struct ConventionalTorpedoRuntimeState final
{
    WeaponRuntimeState weapon{};
    MovementDomain movementDomain = MovementDomain::Attached;
    Physics::PhysicsVector3 positionMeters{};
    float headingRadians = 0.0F;
    float speedMetersPerSecond = 0.0F;
    double lastUpdateTimeSeconds = 0.0;
    std::optional<std::uint64_t> guidanceTrackId{};
    std::optional<Physics::PhysicsVector3> guidanceAimPointMeters{};
    std::optional<float> guidancePositionUncertaintyMeters{};
};

[[nodiscard]] inline float WrapWeaponHeading(const float radians) noexcept
{
    return std::remainder(radians, 6.2831853F);
}

[[nodiscard]] inline std::expected<void, std::string> ValidateConventionalTorpedoDefinition(
    const ConventionalTorpedoDefinition& definition)
{
    const auto weaponValid = ValidateWeaponDefinition(definition.weapon);
    if (!weaponValid)
    {
        return std::unexpected(weaponValid.error());
    }
    if (!std::isfinite(definition.underwaterSpeedMetersPerSecond) ||
        definition.underwaterSpeedMetersPerSecond <= 0.0F ||
        !std::isfinite(definition.maximumTurnRateRadiansPerSecond) ||
        definition.maximumTurnRateRadiansPerSecond <= 0.0F ||
        definition.maximumTurnRateRadiansPerSecond > 3.1415927F)
    {
        return std::unexpected("conventional torpedo movement definition is invalid");
    }
    return {};
}

[[nodiscard]] inline std::expected<ConventionalTorpedoRuntimeState, std::string> CreateLaunchedConventionalTorpedo(
    const ConventionalTorpedoDefinition& definition,
    const WeaponRuntimeState& launchedWeapon,
    const Physics::PhysicsVector3& launchPositionMeters,
    const float launchHeadingRadians,
    const Perception::Track& targetTrack,
    const double simulationTimeSeconds)
{
    const auto definitionValid = ValidateConventionalTorpedoDefinition(definition);
    if (!definitionValid)
    {
        return std::unexpected(definitionValid.error());
    }
    if (launchedWeapon.definitionId != definition.weapon.id || launchedWeapon.phase != WeaponPhase::Launched ||
        !launchedWeapon.targetTrackId || *launchedWeapon.targetTrackId != targetTrack.trackId)
    {
        return std::unexpected("conventional torpedo requires a matching launched weapon and target track");
    }
    if (!launchPositionMeters.IsFinite() || !std::isfinite(launchHeadingRadians) ||
        !std::isfinite(simulationTimeSeconds) || simulationTimeSeconds < launchedWeapon.lastUpdateTimeSeconds)
    {
        return std::unexpected("conventional torpedo launch state is invalid or time-reversing");
    }

    const auto targetValid = ValidateTrackForWeapon(definition.weapon, targetTrack);
    if (!targetValid)
    {
        return std::unexpected(targetValid.error());
    }

    WeaponRuntimeState synchronizedWeapon = launchedWeapon;
    synchronizedWeapon.lastUpdateTimeSeconds = simulationTimeSeconds;

    return ConventionalTorpedoRuntimeState{
        .weapon = synchronizedWeapon,
        .movementDomain = MovementDomain::Underwater,
        .positionMeters = launchPositionMeters,
        .headingRadians = WrapWeaponHeading(launchHeadingRadians),
        .speedMetersPerSecond = definition.underwaterSpeedMetersPerSecond,
        .lastUpdateTimeSeconds = simulationTimeSeconds,
        .guidanceTrackId = targetTrack.trackId,
        .guidanceAimPointMeters = targetTrack.estimatedPositionMeters,
        .guidancePositionUncertaintyMeters = targetTrack.positionUncertaintyMeters};
}

[[nodiscard]] inline std::expected<void, std::string> UpdateConventionalTorpedoGuidance(
    const ConventionalTorpedoDefinition& definition,
    ConventionalTorpedoRuntimeState& state,
    const std::optional<Perception::Track>& perceivedTrack,
    const double simulationTimeSeconds)
{
    const auto definitionValid = ValidateConventionalTorpedoDefinition(definition);
    if (!definitionValid)
    {
        return std::unexpected(definitionValid.error());
    }
    if (state.weapon.definitionId != definition.weapon.id || state.weapon.phase != WeaponPhase::Launched ||
        state.movementDomain != MovementDomain::Underwater || !state.positionMeters.IsFinite() ||
        !std::isfinite(state.headingRadians) || !std::isfinite(state.speedMetersPerSecond) ||
        state.speedMetersPerSecond <= 0.0F || !std::isfinite(simulationTimeSeconds) ||
        simulationTimeSeconds < state.lastUpdateTimeSeconds || simulationTimeSeconds < state.weapon.lastUpdateTimeSeconds)
    {
        return std::unexpected("conventional torpedo runtime is invalid or time-reversing");
    }

    if (perceivedTrack)
    {
        if (!state.guidanceTrackId || perceivedTrack->trackId != *state.guidanceTrackId)
        {
            return std::unexpected("guidance update does not match the torpedo's perceived target track");
        }
        const auto targetValid = ValidateTrackForWeapon(definition.weapon, *perceivedTrack);
        if (targetValid)
        {
            state.guidanceAimPointMeters = perceivedTrack->estimatedPositionMeters;
            state.guidancePositionUncertaintyMeters = perceivedTrack->positionUncertaintyMeters;
        }
    }

    const double deltaSeconds = simulationTimeSeconds - state.lastUpdateTimeSeconds;
    if (deltaSeconds == 0.0)
    {
        return {};
    }

    if (state.guidanceAimPointMeters)
    {
        const double dx = static_cast<double>(state.guidanceAimPointMeters->x) - state.positionMeters.x;
        const double dy = static_cast<double>(state.guidanceAimPointMeters->y) - state.positionMeters.y;
        if (std::isfinite(dx) && std::isfinite(dy) && (std::abs(dx) > 1.0e-6 || std::abs(dy) > 1.0e-6))
        {
            const float desiredHeading = static_cast<float>(std::atan2(dy, dx));
            const float headingDelta = WrapWeaponHeading(desiredHeading - state.headingRadians);
            const float maximumTurn = definition.maximumTurnRateRadiansPerSecond * static_cast<float>(deltaSeconds);
            state.headingRadians = WrapWeaponHeading(
                state.headingRadians + std::clamp(headingDelta, -maximumTurn, maximumTurn));
        }
    }

    const float distanceMeters = state.speedMetersPerSecond * static_cast<float>(deltaSeconds);
    state.positionMeters.x += static_cast<float>(std::cos(static_cast<double>(state.headingRadians))) * distanceMeters;
    state.positionMeters.y += static_cast<float>(std::sin(static_cast<double>(state.headingRadians))) * distanceMeters;
    state.lastUpdateTimeSeconds = simulationTimeSeconds;
    state.weapon.lastUpdateTimeSeconds = simulationTimeSeconds;
    return {};
}
} // namespace DeepRun::Weapons
