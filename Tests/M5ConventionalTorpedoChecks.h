#pragma once

#include "Simulation/Weapons/ConventionalTorpedo.h"

#include <cmath>
#include <optional>

namespace DeepRun::Tests
{
namespace M5ConventionalTorpedoDetail
{
[[nodiscard]] inline Perception::Track MakeSpatialTrack(
    const std::uint64_t trackId,
    const Physics::PhysicsVector3& positionMeters,
    const float uncertaintyMeters = 50.0F)
{
    return Perception::Track{
        .trackId = trackId,
        .contactId = 1U,
        .lifecycle = Perception::TrackLifecycleState::Confirmed,
        .estimatedPositionMeters = positionMeters,
        .positionUncertaintyMeters = uncertaintyMeters,
        .estimatedVelocityMetersPerSecond = std::nullopt,
        .estimatedBearingRadians = 0.0F,
        .bearingUncertaintyRadians = 0.04F,
        .confidence = 0.90F,
        .observationCount = 3U,
        .firstObservationTimeSeconds = 0.0,
        .lastObservationTimeSeconds = 0.0};
}
}

[[nodiscard]] inline bool RunM5ConventionalTorpedoChecks()
{
    using namespace M5ConventionalTorpedoDetail;
    using namespace Weapons;

    const ConventionalTorpedoDefinition definition{
        .weapon = WeaponDefinition{
            .id = "m5.conventional-heavyweight",
            .preparationSeconds = 0.0,
            .targeting = WeaponTargetingRequirements{
                .minimumTrackConfidence = 0.70F,
                .maximumBearingUncertaintyRadians = 0.10F,
                .maximumPositionUncertaintyMeters = 150.0F,
                .requiresEstimatedPosition = true,
                .allowCoastingTrack = false}},
        .underwaterSpeedMetersPerSecond = 20.0F,
        .maximumTurnRateRadiansPerSecond = 0.25F};

    const auto initialTrack = MakeSpatialTrack(42U, {.x = 1000.0F, .y = 0.0F, .z = 0.0F});

    auto weaponResult = CreateWeaponRuntime(definition.weapon, 0.0);
    if (!weaponResult)
    {
        return false;
    }
    auto weapon = *weaponResult;
    if (!PrepareWeapon(definition.weapon, weapon, 0.0) || weapon.phase != WeaponPhase::Ready ||
        !AssignWeaponTarget(definition.weapon, weapon, initialTrack, 0.0) ||
        !LaunchWeapon(definition.weapon, weapon, 0.0))
    {
        return false;
    }

    auto torpedoResult = CreateLaunchedConventionalTorpedo(
        definition, weapon, {.x = 0.0F, .y = 0.0F, .z = 0.0F}, 0.0F, initialTrack, 0.0);
    if (!torpedoResult)
    {
        return false;
    }
    auto torpedo = *torpedoResult;
    if (torpedo.movementDomain != MovementDomain::Underwater || torpedo.guidanceTrackId != initialTrack.trackId ||
        torpedo.guidanceAimPointMeters != initialTrack.estimatedPositionMeters ||
        torpedo.weapon.lastUpdateTimeSeconds != torpedo.lastUpdateTimeSeconds)
    {
        return false;
    }

    if (!UpdateConventionalTorpedoGuidance(definition, torpedo, std::nullopt, 1.0) ||
        std::abs(torpedo.positionMeters.x - 20.0F) > 0.01F || std::abs(torpedo.positionMeters.y) > 0.01F)
    {
        return false;
    }

    const auto movedTrack = MakeSpatialTrack(42U, {.x = 1000.0F, .y = 1000.0F, .z = 0.0F});
    if (!UpdateConventionalTorpedoGuidance(definition, torpedo, movedTrack, 2.0) ||
        std::abs(torpedo.headingRadians - 0.25F) > 0.001F ||
        torpedo.guidanceAimPointMeters != movedTrack.estimatedPositionMeters)
    {
        return false;
    }

    auto tooUncertainTrack = movedTrack;
    tooUncertainTrack.estimatedPositionMeters = Physics::PhysicsVector3{.x = 5000.0F, .y = -5000.0F, .z = 0.0F};
    tooUncertainTrack.positionUncertaintyMeters = 500.0F;
    const auto acceptedAimPoint = torpedo.guidanceAimPointMeters;
    if (!UpdateConventionalTorpedoGuidance(definition, torpedo, tooUncertainTrack, 3.0) ||
        torpedo.guidanceAimPointMeters != acceptedAimPoint)
    {
        return false;
    }

    auto wrongTrack = movedTrack;
    wrongTrack.trackId = 99U;
    const double beforeWrongTrackTime = torpedo.lastUpdateTimeSeconds;
    if (UpdateConventionalTorpedoGuidance(definition, torpedo, wrongTrack, 4.0) ||
        torpedo.lastUpdateTimeSeconds != beforeWrongTrackTime)
    {
        return false;
    }

    if (UpdateConventionalTorpedoGuidance(definition, torpedo, std::nullopt, 2.5))
    {
        return false;
    }

    auto invalidDefinition = definition;
    invalidDefinition.underwaterSpeedMetersPerSecond = 0.0F;
    if (ValidateConventionalTorpedoDefinition(invalidDefinition))
    {
        return false;
    }

    auto unlaunchedWeaponResult = CreateWeaponRuntime(definition.weapon, 10.0);
    if (!unlaunchedWeaponResult || CreateLaunchedConventionalTorpedo(
        definition, *unlaunchedWeaponResult, {.x = 0.0F, .y = 0.0F, .z = 0.0F}, 0.0F, initialTrack, 10.0))
    {
        return false;
    }

    return true;
}
} // namespace DeepRun::Tests
