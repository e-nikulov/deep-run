#pragma once

#include "Game/Combat/SimpleDestroyerRuntime.h"
#include "Simulation/Weapons/ConventionalTorpedo.h"

#include <cmath>
#include <optional>

namespace DeepRun::Tests
{
namespace M5SimpleDestroyerRuntimeDetail
{
[[nodiscard]] inline Acoustics::AcousticSpectrum UniformSpectrum(const float level) noexcept
{
    return Acoustics::AcousticSpectrum{.levelDb = {level, level, level, level}};
}

[[nodiscard]] inline Game::Combat::SimpleDestroyerDefinition MakeDestroyerDefinition()
{
    return Game::Combat::SimpleDestroyerDefinition{
        .id = "m5.destroyer-proxy",
        .collisionHalfExtentsMeters = {.x = 10.0F, .y = 2.0F, .z = 3.0F},
        .massKilograms = 2'000'000.0F,
        .cruiseVelocityXMetersPerSecond = -4.0F,
        .bodyCenterBelowSurfaceMeters = 1.5F,
        .maximumIntegrity = 100.0F,
        .continuousSourceLevelDb = UniformSpectrum(145.0F),
        .passiveSensorId = "M5_DESTROYER_PASSIVE_ARRAY",
        .ambientNoiseLevelDb = UniformSpectrum(40.0F),
        .selfNoiseLevelDb = UniformSpectrum(43.0F),
        .sensitivityDb = UniformSpectrum(0.0F),
        .minimumPeakSnrDb = 3.0F,
        .weapon = Weapons::WeaponDefinition{
            .id = "m5.destroyer-weapon",
            .preparationSeconds = 2.0,
            .targeting = Weapons::WeaponTargetingRequirements{
                .minimumTrackConfidence = 0.70F,
                .maximumBearingUncertaintyRadians = 0.10F,
                .maximumPositionUncertaintyMeters = 150.0F,
                .requiresEstimatedPosition = true,
                .allowCoastingTrack = false}},
        .combat = Game::Combat::SimpleDestroyerCombatConfig{
            .minimumAwarenessConfidence = 0.35F,
            .maximumAwarenessBearingUncertaintyRadians = 0.30F,
            .allowCoastingAwareness = false}};
}

[[nodiscard]] inline Perception::Track MakeDestroyerTrack(
    const std::uint64_t trackId,
    const Physics::PhysicsVector3& positionMeters)
{
    return Perception::Track{
        .trackId = trackId,
        .contactId = trackId + 100U,
        .lifecycle = Perception::TrackLifecycleState::Confirmed,
        .estimatedPositionMeters = positionMeters,
        .positionUncertaintyMeters = 30.0F,
        .estimatedVelocityMetersPerSecond = std::nullopt,
        .estimatedBearingRadians = 0.0F,
        .bearingUncertaintyRadians = 0.05F,
        .confidence = 0.90F,
        .observationCount = 4U,
        .firstObservationTimeSeconds = 0.0,
        .lastObservationTimeSeconds = 1.0};
}
}

// Reuses the caller-owned PhysicsWorld instead of creating another Jolt authority in the same process. This
// mirrors the real Engine composition: one PhysicsWorld owns all combat bodies and all weapon collision queries.
[[nodiscard]] inline bool RunM5SimpleDestroyerRuntimeChecks(Physics::PhysicsWorld& physicsWorld)
{
    using namespace M5SimpleDestroyerRuntimeDetail;
    using namespace Game::Combat;
    using namespace Weapons;

    if (!physicsWorld.IsInitialized())
    {
        return false;
    }

    const auto destroyerDefinition = MakeDestroyerDefinition();
    auto destroyerResult = CreateSimpleDestroyerRuntime(
        destroyerDefinition,
        physicsWorld,
        0.0F,
        60.0F,
        0.0F,
        0.0);
    if (!destroyerResult)
    {
        return false;
    }
    auto destroyer = *destroyerResult;

    const auto initialState = physicsWorld.GetBodyState(destroyer.body);
    if (!initialState || std::abs(initialState->position.x - 60.0F) > 0.001F ||
        std::abs(initialState->position.y + 1.5F) > 0.001F || std::abs(initialState->position.z) > 0.001F ||
        std::abs(initialState->linearVelocity.x + 4.0F) > 0.001F ||
        std::abs(initialState->linearVelocity.y) > 0.001F || std::abs(initialState->linearVelocity.z) > 0.001F ||
        destroyer.integrity.body != destroyer.body || destroyer.integrity.remainingIntegrity != 100.0F ||
        destroyer.weapon.phase != WeaponPhase::Stored)
    {
        return false;
    }

    const auto initialAcoustics = SampleSimpleDestroyerAcoustics(destroyerDefinition, destroyer, physicsWorld);
    if (!initialAcoustics || initialAcoustics->emitter.positionMeters != initialState->position ||
        initialAcoustics->emitter.velocityMetersPerSecond != initialState->linearVelocity ||
        initialAcoustics->passiveReceiver.positionMeters != initialState->position ||
        initialAcoustics->passiveReceiver.sensorId != destroyerDefinition.passiveSensorId)
    {
        return false;
    }

    for (int step = 0; step < 60; ++step)
    {
        physicsWorld.Step(1.0F / 60.0F);
    }
    const auto cruisedState = physicsWorld.GetBodyState(destroyer.body);
    if (!cruisedState || std::abs(cruisedState->position.x - 56.0F) > 0.05F ||
        std::abs(cruisedState->position.y + 1.5F) > 0.001F || std::abs(cruisedState->position.z) > 0.001F ||
        std::abs(cruisedState->linearVelocity.x + 4.0F) > 0.01F ||
        std::abs(cruisedState->linearVelocity.y) > 0.001F || std::abs(cruisedState->linearVelocity.z) > 0.001F)
    {
        return false;
    }

    const auto cruisedAcoustics = SampleSimpleDestroyerAcoustics(destroyerDefinition, destroyer, physicsWorld);
    if (!cruisedAcoustics || cruisedAcoustics->emitter.positionMeters != cruisedState->position ||
        cruisedAcoustics->passiveReceiver.positionMeters != cruisedState->position)
    {
        return false;
    }

    // Close the headless combat composition: a conventional torpedo receives only a perceived spatial Track,
    // but its physical sweep may then hit the destroyer's real Jolt body and produce body-bound damage.
    const WeaponDefinition playerWeapon{
        .id = "m5.player-heavyweight-vs-destroyer",
        .preparationSeconds = 0.0,
        .targeting = WeaponTargetingRequirements{
            .minimumTrackConfidence = 0.70F,
            .maximumBearingUncertaintyRadians = 0.10F,
            .maximumPositionUncertaintyMeters = 150.0F,
            .requiresEstimatedPosition = true,
            .allowCoastingTrack = false}};
    const ConventionalTorpedoDefinition torpedoDefinition{
        .weapon = playerWeapon,
        .underwaterSpeedMetersPerSecond = 40.0F,
        .maximumTurnRateRadiansPerSecond = 0.50F,
        .collisionHalfExtentsMeters = {.x = 2.0F, .y = 0.25F, .z = 0.25F},
        .directImpactDamage = 60.0F,
        .explosionRadiusMeters = 8.0F};
    const auto perceivedDestroyerTrack = MakeDestroyerTrack(500U, cruisedState->position);

    auto playerWeaponResult = CreateWeaponRuntime(playerWeapon, 1.0);
    if (!playerWeaponResult)
    {
        return false;
    }
    auto playerWeaponRuntime = *playerWeaponResult;
    if (!PrepareWeapon(playerWeapon, playerWeaponRuntime, 1.0) ||
        !AssignWeaponTarget(playerWeapon, playerWeaponRuntime, perceivedDestroyerTrack, 1.0) ||
        !LaunchWeapon(playerWeapon, playerWeaponRuntime, 1.0))
    {
        return false;
    }

    auto torpedoResult = CreateLaunchedConventionalTorpedo(
        torpedoDefinition,
        playerWeaponRuntime,
        {.x = 0.0F, .y = cruisedState->position.y, .z = cruisedState->position.z},
        0.0F,
        perceivedDestroyerTrack,
        1.0);
    if (!torpedoResult)
    {
        return false;
    }
    auto torpedo = *torpedoResult;
    const auto impact = AdvanceConventionalTorpedoWithCollision(
        torpedoDefinition,
        torpedo,
        std::nullopt,
        physicsWorld,
        3.0);
    if (!impact || !*impact || (**impact).physicsHit.body != destroyer.body ||
        torpedo.movementDomain != MovementDomain::Spent)
    {
        return false;
    }

    if (!ApplySimpleDestroyerDamage(destroyerDefinition, destroyer, (**impact).damage) ||
        std::abs(destroyer.integrity.remainingIntegrity - 40.0F) > 0.001F || destroyer.integrity.destroyed)
    {
        return false;
    }

    const auto awarenessTrack = Perception::Track{
        .trackId = 600U,
        .contactId = 601U,
        .lifecycle = Perception::TrackLifecycleState::Confirmed,
        .estimatedPositionMeters = std::nullopt,
        .positionUncertaintyMeters = std::nullopt,
        .estimatedVelocityMetersPerSecond = std::nullopt,
        .estimatedBearingRadians = -0.4F,
        .bearingUncertaintyRadians = 0.10F,
        .confidence = 0.80F,
        .observationCount = 3U,
        .firstObservationTimeSeconds = 1.0,
        .lastObservationTimeSeconds = 4.0};
    const auto prepareDecision = AdvanceSimpleDestroyerCombatRuntime(
        destroyerDefinition, destroyer, {awarenessTrack}, 4.0);
    if (!prepareDecision || prepareDecision->action != SimpleDestroyerCombatAction::PrepareWeapon ||
        destroyer.weapon.phase != WeaponPhase::Preparing)
    {
        return false;
    }

    const DeepRun::Combat::CombatDamageEvent finishingDamage{
        .targetBody = destroyer.body,
        .positionMeters = cruisedState->position,
        .damage = 50.0F,
        .simulationTimeSeconds = 5.0};
    if (!ApplySimpleDestroyerDamage(destroyerDefinition, destroyer, finishingDamage) ||
        !destroyer.integrity.destroyed || destroyer.integrity.remainingIntegrity != 0.0F ||
        AdvanceSimpleDestroyerCombatRuntime(destroyerDefinition, destroyer, {awarenessTrack}, 5.1))
    {
        return false;
    }

    if (!physicsWorld.DestroyBody(destroyer.body) ||
        SampleSimpleDestroyerAcoustics(destroyerDefinition, destroyer, physicsWorld))
    {
        return false;
    }

    return true;
}
} // namespace DeepRun::Tests
