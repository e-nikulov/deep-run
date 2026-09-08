#pragma once

#include "Engine/Diagnostics/Logger.h"
#include "Engine/Physics/PhysicsWorld.h"
#include "Simulation/Combat/CombatIntegrity.h"
#include "Simulation/Weapons/ConventionalTorpedo.h"

#include <cmath>
#include <optional>

namespace DeepRun::Tests
{
namespace M5CombatImpactDetail
{
[[nodiscard]] inline Perception::Track MakeImpactTrack()
{
    return Perception::Track{
        .trackId = 700U,
        .contactId = 70U,
        .lifecycle = Perception::TrackLifecycleState::Confirmed,
        .estimatedPositionMeters = Physics::PhysicsVector3{.x = 1000.0F, .y = 0.0F, .z = 0.0F},
        .positionUncertaintyMeters = 25.0F,
        .estimatedVelocityMetersPerSecond = std::nullopt,
        .estimatedBearingRadians = 0.0F,
        .bearingUncertaintyRadians = 0.03F,
        .confidence = 0.95F,
        .observationCount = 4U,
        .firstObservationTimeSeconds = 0.0,
        .lastObservationTimeSeconds = 0.0};
}

[[nodiscard]] inline Weapons::ConventionalTorpedoDefinition MakeImpactDefinition()
{
    return Weapons::ConventionalTorpedoDefinition{
        .weapon = Weapons::WeaponDefinition{
            .id = "m5.impact-heavyweight",
            .preparationSeconds = 0.0,
            .targeting = Weapons::WeaponTargetingRequirements{
                .minimumTrackConfidence = 0.70F,
                .maximumBearingUncertaintyRadians = 0.10F,
                .maximumPositionUncertaintyMeters = 150.0F,
                .requiresEstimatedPosition = true,
                .allowCoastingTrack = false}},
        .underwaterSpeedMetersPerSecond = 20.0F,
        .maximumTurnRateRadiansPerSecond = 0.25F,
        .collisionHalfExtentsMeters = {.x = 2.0F, .y = 0.25F, .z = 0.25F},
        .directImpactDamage = 75.0F,
        .explosionRadiusMeters = 8.0F};
}
}

[[nodiscard]] inline bool RunM5CombatImpactChecks()
{
    using namespace M5CombatImpactDetail;
    using namespace Weapons;

    Diagnostics::Logger logger;
    Physics::PhysicsWorld physicsWorld(logger);
    if (!physicsWorld.Initialize())
    {
        return false;
    }

    const Physics::PhysicsBodyHandle launchPlatformBody = physicsWorld.CreateStaticBoxBody(
        Physics::StaticBoxBodyCreateInfo{
            .halfExtents = {.x = 1.0F, .y = 1.0F, .z = 1.0F},
            .position = {.x = 0.0F, .y = 0.0F, .z = 0.0F}});
    const Physics::PhysicsBodyHandle targetBody = physicsWorld.CreateStaticBoxBody(
        Physics::StaticBoxBodyCreateInfo{
            .halfExtents = {.x = 2.0F, .y = 2.0F, .z = 2.0F},
            .position = {.x = 30.0F, .y = 0.0F, .z = 0.0F}});
    if (!launchPlatformBody.IsValid() || !targetBody.IsValid())
    {
        return false;
    }

    // Prove the generic query itself: without filtering, starting inside the launch platform reports it;
    // explicitly ignoring that body lets the same sweep reach the later physical target.
    Physics::PhysicsBoxSweepQuery genericSweep{
        .halfExtentsMeters = {.x = 1.0F, .y = 0.25F, .z = 0.25F},
        .startPositionMeters = {.x = 0.0F, .y = 0.0F, .z = 0.0F},
        .orientation = {},
        .displacementMeters = {.x = 40.0F, .y = 0.0F, .z = 0.0F}};
    const auto launchHit = physicsWorld.SweepBoxClosest(genericSweep);
    if (!launchHit || !*launchHit || (**launchHit).body != launchPlatformBody || (**launchHit).fraction != 0.0F)
    {
        return false;
    }

    genericSweep.ignoredBody = launchPlatformBody;
    const auto targetHit = physicsWorld.SweepBoxClosest(genericSweep);
    if (!targetHit || !*targetHit || (**targetHit).body != targetBody ||
        std::abs((**targetHit).fraction - 0.675F) > 0.01F ||
        std::abs((**targetHit).positionMeters.x - 27.0F) > 0.1F)
    {
        return false;
    }

    auto invalidSweep = genericSweep;
    invalidSweep.displacementMeters = {};
    const auto invalidSweepResult = physicsWorld.SweepBoxClosest(invalidSweep);
    if (invalidSweepResult || invalidSweepResult.error().code != Physics::PhysicsErrorCode::InvalidInput)
    {
        return false;
    }

    const auto definition = MakeImpactDefinition();
    const auto track = MakeImpactTrack();
    auto weaponResult = CreateWeaponRuntime(definition.weapon, 0.0);
    if (!weaponResult)
    {
        return false;
    }
    auto weapon = *weaponResult;
    if (!PrepareWeapon(definition.weapon, weapon, 0.0) ||
        !AssignWeaponTarget(definition.weapon, weapon, track, 0.0) ||
        !LaunchWeapon(definition.weapon, weapon, 0.0))
    {
        return false;
    }

    auto torpedoResult = CreateLaunchedConventionalTorpedo(
        definition,
        weapon,
        {.x = 0.0F, .y = 0.0F, .z = 0.0F},
        0.0F,
        track,
        0.0);
    if (!torpedoResult)
    {
        return false;
    }
    auto torpedo = *torpedoResult;

    auto integrityResult = Combat::CreateCombatIntegrity(targetBody, 100.0F, 0.0);
    if (!integrityResult)
    {
        return false;
    }
    auto integrity = *integrityResult;

    const auto impactResult = AdvanceConventionalTorpedoWithCollision(
        definition,
        torpedo,
        std::nullopt,
        physicsWorld,
        2.0,
        launchPlatformBody);
    if (!impactResult || !*impactResult)
    {
        return false;
    }

    const auto& impact = **impactResult;
    if (impact.physicsHit.body != targetBody || torpedo.movementDomain != MovementDomain::Spent ||
        torpedo.impactedBody != targetBody || torpedo.speedMetersPerSecond != 0.0F ||
        std::abs(torpedo.positionMeters.x - 26.0F) > 0.1F ||
        impact.damage.targetBody != targetBody || impact.damage.damage != definition.directImpactDamage ||
        impact.explosion.radiusMeters != definition.explosionRadiusMeters ||
        impact.explosion.simulationTimeSeconds != 2.0)
    {
        return false;
    }

    if (!Combat::ApplyCombatDamage(integrity, impact.damage) ||
        std::abs(integrity.remainingIntegrity - 25.0F) > 0.001F || integrity.destroyed)
    {
        return false;
    }

    auto wrongBodyDamage = impact.damage;
    wrongBodyDamage.targetBody = launchPlatformBody;
    if (Combat::ApplyCombatDamage(integrity, wrongBodyDamage))
    {
        return false;
    }

    Combat::CombatDamageEvent finishingDamage{
        .targetBody = targetBody,
        .positionMeters = impact.physicsHit.positionMeters,
        .damage = 30.0F,
        .simulationTimeSeconds = 3.0};
    if (!Combat::ApplyCombatDamage(integrity, finishingDamage) || !integrity.destroyed ||
        integrity.remainingIntegrity != 0.0F)
    {
        return false;
    }

    finishingDamage.simulationTimeSeconds = 2.5;
    if (Combat::ApplyCombatDamage(integrity, finishingDamage))
    {
        return false;
    }

    // A confirmed impact consumes the torpedo. It cannot generate a second damage/explosion event later.
    if (AdvanceConventionalTorpedoWithCollision(
            definition,
            torpedo,
            std::nullopt,
            physicsWorld,
            3.0,
            launchPlatformBody))
    {
        return false;
    }

    return true;
}
} // namespace DeepRun::Tests
