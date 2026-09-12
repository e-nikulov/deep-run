#pragma once

#include "Engine/Physics/PhysicsWorld.h"
#include "Simulation/Combat/CombatIntegrity.h"
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
    Spent,
};

enum class ConventionalTorpedoTerminalReason
{
    None,
    Impact,
    EnduranceExpired,
};

struct ConventionalTorpedoDefinition final
{
    WeaponDefinition weapon{};
    float underwaterSpeedMetersPerSecond = 20.0F;
    float maximumTurnRateRadiansPerSecond = 0.25F;
    // GAME POLICY only: finite propulsion/energy endurance prevents a missed weapon from pursuing forever.
    // This is not an exact endurance/range claim for any real torpedo.
    double maximumRunTimeSeconds = 300.0;

    // Gameplay-authored 2.5D depth-course limit. pi/2 preserves the pre-M5-C behaviour by default; concrete
    // scenarios may choose a smaller value to prevent a conventional underwater weapon from visually behaving
    // like a missile. This is not claimed real-world torpedo performance data.
    float maximumVerticalCourseAngleRadians = 1.5707963F;

    // M5-D coarse physical/payload representation. These are gameplay-authored values, not claimed real-world
    // performance data. The box is swept by PhysicsWorld each fixed movement update.
    Physics::PhysicsVector3 collisionHalfExtentsMeters{2.0F, 0.25F, 0.25F};
    float directImpactDamage = 75.0F;
    float explosionRadiusMeters = 8.0F;
};

// Bounded M5-C/D runtime: authoritative weapon phase remains in WeaponRuntimeState, while movement owns only
// underwater kinematics, perceived-world guidance and terminal physical-impact state. No target entity handle or
// Transform exists before PhysicsWorld reports an actual collision.
struct ConventionalTorpedoRuntimeState final
{
    WeaponRuntimeState weapon{};
    MovementDomain movementDomain = MovementDomain::Attached;
    Physics::PhysicsVector3 positionMeters{};
    float headingRadians = 0.0F;
    float speedMetersPerSecond = 0.0F;
    double launchTimeSeconds = 0.0;
    double lastUpdateTimeSeconds = 0.0;
    ConventionalTorpedoTerminalReason terminalReason = ConventionalTorpedoTerminalReason::None;
    std::optional<std::uint64_t> guidanceTrackId{};
    std::optional<Physics::PhysicsVector3> guidanceAimPointMeters{};
    std::optional<float> guidancePositionUncertaintyMeters{};
    std::optional<Physics::PhysicsBodyHandle> impactedBody{};
};

struct ConventionalTorpedoImpact final
{
    Physics::PhysicsSweepHit physicsHit{};
    Combat::CombatDamageEvent damage{};
    Combat::CombatExplosionEvent explosion{};
};

[[nodiscard]] inline float WrapWeaponHeading(const float radians) noexcept
{
    return std::remainder(radians, 6.2831853F);
}

// Limits the course angle relative to the nearest horizontal direction (+X or -X). The generic torpedo remains
// free to steer left/right in the 2.5D gameplay plane, but its vertical component cannot exceed the authored
// conventional-underwater profile. A value of pi/2 is effectively unrestricted and preserves legacy tests.
[[nodiscard]] inline float ClampConventionalTorpedoVerticalCourse(
    const float headingRadians,
    const float maximumVerticalCourseAngleRadians) noexcept
{
    constexpr float halfPi = 1.5707963F;
    constexpr float pi = 3.1415927F;
    const float wrapped = WrapWeaponHeading(headingRadians);
    if (wrapped >= -halfPi && wrapped <= halfPi)
    {
        return std::clamp(wrapped, -maximumVerticalCourseAngleRadians, maximumVerticalCourseAngleRadians);
    }

    const float horizontalHeading = wrapped > 0.0F ? pi : -pi;
    const float relativeVerticalCourse = WrapWeaponHeading(wrapped - horizontalHeading);
    return WrapWeaponHeading(horizontalHeading + std::clamp(
        relativeVerticalCourse, -maximumVerticalCourseAngleRadians, maximumVerticalCourseAngleRadians));
}

[[nodiscard]] inline Physics::PhysicsQuaternion WeaponHeadingQuaternion(const float headingRadians) noexcept
{
    const float halfAngle = 0.5F * headingRadians;
    return Physics::PhysicsQuaternion{
        .x = 0.0F,
        .y = 0.0F,
        .z = static_cast<float>(std::sin(static_cast<double>(halfAngle))),
        .w = static_cast<float>(std::cos(static_cast<double>(halfAngle)))};
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
        definition.maximumTurnRateRadiansPerSecond > 3.1415927F ||
        !std::isfinite(definition.maximumRunTimeSeconds) || definition.maximumRunTimeSeconds <= 0.0 ||
        !std::isfinite(definition.maximumVerticalCourseAngleRadians) ||
        definition.maximumVerticalCourseAngleRadians <= 0.0F ||
        definition.maximumVerticalCourseAngleRadians > 1.5707963F ||
        !definition.collisionHalfExtentsMeters.IsFinite() ||
        definition.collisionHalfExtentsMeters.x <= 0.0F || definition.collisionHalfExtentsMeters.y <= 0.0F ||
        definition.collisionHalfExtentsMeters.z <= 0.0F || !std::isfinite(definition.directImpactDamage) ||
        definition.directImpactDamage <= 0.0F || !std::isfinite(definition.explosionRadiusMeters) ||
        definition.explosionRadiusMeters <= 0.0F)
    {
        return std::unexpected("conventional torpedo movement/collision/payload definition is invalid");
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
        .headingRadians = ClampConventionalTorpedoVerticalCourse(
            launchHeadingRadians, definition.maximumVerticalCourseAngleRadians),
        .speedMetersPerSecond = definition.underwaterSpeedMetersPerSecond,
        .launchTimeSeconds = simulationTimeSeconds,
        .lastUpdateTimeSeconds = simulationTimeSeconds,
        .terminalReason = ConventionalTorpedoTerminalReason::None,
        .guidanceTrackId = targetTrack.trackId,
        .guidanceAimPointMeters = targetTrack.estimatedPositionMeters,
        .guidancePositionUncertaintyMeters = targetTrack.positionUncertaintyMeters,
        .impactedBody = std::nullopt};
}

[[nodiscard]] inline std::expected<bool, std::string> ExpireConventionalTorpedoEnduranceIfNeeded(
    const ConventionalTorpedoDefinition& definition,
    ConventionalTorpedoRuntimeState& state,
    const double simulationTimeSeconds)
{
    if (!std::isfinite(simulationTimeSeconds) || !std::isfinite(state.launchTimeSeconds) ||
        simulationTimeSeconds < state.lastUpdateTimeSeconds || simulationTimeSeconds < state.launchTimeSeconds)
    {
        return std::unexpected("conventional torpedo endurance time is invalid or time-reversing");
    }
    if (state.movementDomain != MovementDomain::Underwater || state.impactedBody.has_value())
    {
        return false;
    }
    const double elapsedSeconds = simulationTimeSeconds - state.launchTimeSeconds;
    if (elapsedSeconds + 1.0e-9 < definition.maximumRunTimeSeconds)
    {
        return false;
    }
    state.speedMetersPerSecond = 0.0F;
    state.movementDomain = MovementDomain::Spent;
    state.terminalReason = ConventionalTorpedoTerminalReason::EnduranceExpired;
    state.lastUpdateTimeSeconds = simulationTimeSeconds;
    state.weapon.lastUpdateTimeSeconds = simulationTimeSeconds;
    return true;
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
    const auto expired = ExpireConventionalTorpedoEnduranceIfNeeded(definition, state, simulationTimeSeconds);
    if (!expired)
    {
        return std::unexpected(expired.error());
    }
    if (*expired)
    {
        return {};
    }
    if (state.weapon.definitionId != definition.weapon.id || state.weapon.phase != WeaponPhase::Launched ||
        state.movementDomain != MovementDomain::Underwater || state.impactedBody.has_value() ||
        !state.positionMeters.IsFinite() || !std::isfinite(state.headingRadians) ||
        !std::isfinite(state.speedMetersPerSecond) || state.speedMetersPerSecond <= 0.0F ||
        !std::isfinite(simulationTimeSeconds) || simulationTimeSeconds < state.lastUpdateTimeSeconds ||
        simulationTimeSeconds < state.weapon.lastUpdateTimeSeconds)
    {
        return std::unexpected("conventional torpedo runtime is invalid, spent, or time-reversing");
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
            state.headingRadians = ClampConventionalTorpedoVerticalCourse(
                state.headingRadians, definition.maximumVerticalCourseAngleRadians);
        }
    }

    const float distanceMeters = state.speedMetersPerSecond * static_cast<float>(deltaSeconds);
    state.positionMeters.x += static_cast<float>(std::cos(static_cast<double>(state.headingRadians))) * distanceMeters;
    state.positionMeters.y += static_cast<float>(std::sin(static_cast<double>(state.headingRadians))) * distanceMeters;
    state.lastUpdateTimeSeconds = simulationTimeSeconds;
    state.weapon.lastUpdateTimeSeconds = simulationTimeSeconds;
    return {};
}

// Advances guidance/kinematics on a candidate copy, then asks PhysicsWorld to sweep the authored collision box
// from the previous position to the candidate position. Only a confirmed physics hit can expose a body handle and
// consume the torpedo into Spent state. Query failures do not partially advance authoritative weapon state.
[[nodiscard]] inline std::expected<std::optional<ConventionalTorpedoImpact>, std::string>
AdvanceConventionalTorpedoWithCollision(
    const ConventionalTorpedoDefinition& definition,
    ConventionalTorpedoRuntimeState& state,
    const std::optional<Perception::Track>& perceivedTrack,
    Physics::PhysicsWorld& physicsWorld,
    const double simulationTimeSeconds,
    const Physics::PhysicsBodyHandle ignoredBody = {})
{
    if (state.movementDomain != MovementDomain::Underwater || state.impactedBody.has_value())
    {
        return std::unexpected("spent/non-underwater conventional torpedo cannot advance with collision");
    }

    const Physics::PhysicsVector3 startPosition = state.positionMeters;
    ConventionalTorpedoRuntimeState candidate = state;
    const auto movement = UpdateConventionalTorpedoGuidance(definition, candidate, perceivedTrack, simulationTimeSeconds);
    if (!movement)
    {
        return std::unexpected(movement.error());
    }

    const Physics::PhysicsVector3 displacement{
        .x = candidate.positionMeters.x - startPosition.x,
        .y = candidate.positionMeters.y - startPosition.y,
        .z = candidate.positionMeters.z - startPosition.z};
    if (displacement.x == 0.0F && displacement.y == 0.0F && displacement.z == 0.0F)
    {
        state = candidate;
        return std::optional<ConventionalTorpedoImpact>{};
    }

    const auto sweep = physicsWorld.SweepBoxClosest(Physics::PhysicsBoxSweepQuery{
        .halfExtentsMeters = definition.collisionHalfExtentsMeters,
        .startPositionMeters = startPosition,
        .orientation = WeaponHeadingQuaternion(candidate.headingRadians),
        .displacementMeters = displacement,
        .ignoredBody = ignoredBody});
    if (!sweep)
    {
        return std::unexpected("conventional torpedo physics sweep failed: " + sweep.error().message);
    }
    if (!*sweep)
    {
        state = candidate;
        return std::optional<ConventionalTorpedoImpact>{};
    }

    const Physics::PhysicsSweepHit hit = **sweep;
    candidate.positionMeters = hit.positionMeters;
    candidate.speedMetersPerSecond = 0.0F;
    candidate.movementDomain = MovementDomain::Spent;
    candidate.terminalReason = ConventionalTorpedoTerminalReason::Impact;
    candidate.impactedBody = hit.body;
    candidate.lastUpdateTimeSeconds = simulationTimeSeconds;
    candidate.weapon.lastUpdateTimeSeconds = simulationTimeSeconds;
    state = candidate;

    return std::optional<ConventionalTorpedoImpact>{ConventionalTorpedoImpact{
        .physicsHit = hit,
        .damage = Combat::CombatDamageEvent{
            .targetBody = hit.body,
            .positionMeters = hit.positionMeters,
            .damage = definition.directImpactDamage,
            .simulationTimeSeconds = simulationTimeSeconds},
        .explosion = Combat::CombatExplosionEvent{
            .positionMeters = hit.positionMeters,
            .nominalDamage = definition.directImpactDamage,
            .radiusMeters = definition.explosionRadiusMeters,
            .simulationTimeSeconds = simulationTimeSeconds}}};
}
} // namespace DeepRun::Weapons
