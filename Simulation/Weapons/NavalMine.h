#pragma once

#include "Engine/Physics/PhysicsWorld.h"
#include "Simulation/Combat/CombatIntegrity.h"

#include <cmath>
#include <expected>
#include <optional>
#include <string>

namespace DeepRun::Weapons
{
// M5-I bounded contact-mine hazard. The mine has no perception, target-selection or guidance path: it is a
// physical world object that can detonate only when the generic PhysicsWorld sweep confirms that a moving
// physical proxy reached this mine body. Rich mine types, influence sensors and countermeasures are future work.
struct NavalMineDefinition final
{
    std::string id;
    Physics::PhysicsVector3 collisionHalfExtentsMeters{1.0F, 1.0F, 1.0F};
    float contactDamage = 80.0F;
    float explosionRadiusMeters = 10.0F;
};

struct NavalMineRuntimeState final
{
    std::string definitionId;
    Physics::PhysicsBodyHandle body{};
    bool armed = true;
    bool detonated = false;
    double lastUpdateTimeSeconds = 0.0;
};

struct NavalMineDetonation final
{
    Combat::CombatDamageEvent damage{};
    Combat::CombatExplosionEvent explosion{};
};

[[nodiscard]] inline std::expected<void, std::string> ValidateNavalMineDefinition(
    const NavalMineDefinition& definition)
{
    if (definition.id.empty() || !definition.collisionHalfExtentsMeters.IsFinite() ||
        definition.collisionHalfExtentsMeters.x <= 0.0F ||
        definition.collisionHalfExtentsMeters.y <= 0.0F ||
        definition.collisionHalfExtentsMeters.z <= 0.0F ||
        !std::isfinite(definition.contactDamage) || definition.contactDamage <= 0.0F ||
        !std::isfinite(definition.explosionRadiusMeters) || definition.explosionRadiusMeters <= 0.0F)
    {
        return std::unexpected("naval mine definition is invalid");
    }
    return {};
}

[[nodiscard]] inline std::expected<NavalMineRuntimeState, std::string> CreateNavalMineRuntime(
    const NavalMineDefinition& definition,
    Physics::PhysicsWorld& physicsWorld,
    const Physics::PhysicsVector3& positionMeters,
    const double simulationTimeSeconds)
{
    const auto valid = ValidateNavalMineDefinition(definition);
    if (!valid)
    {
        return std::unexpected(valid.error());
    }
    if (!physicsWorld.IsInitialized() || !positionMeters.IsFinite() ||
        !std::isfinite(simulationTimeSeconds) || simulationTimeSeconds < 0.0)
    {
        return std::unexpected("naval mine creation input is invalid");
    }

    Physics::PhysicsError physicsError;
    const auto body = physicsWorld.CreateStaticBoxBody(
        Physics::StaticBoxBodyCreateInfo{
            .halfExtents = definition.collisionHalfExtentsMeters,
            .position = positionMeters},
        &physicsError);
    if (!body.IsValid())
    {
        return std::unexpected("naval mine physical body creation failed: " + physicsError.message);
    }

    return NavalMineRuntimeState{
        .definitionId = definition.id,
        .body = body,
        .armed = true,
        .detonated = false,
        .lastUpdateTimeSeconds = simulationTimeSeconds};
}

// Evaluates one already-authored moving physical proxy against the mine using PhysicsWorld's backend-authoritative
// closest sweep. `targetBody` is ignored only to prevent the moving proxy from hitting its own Jolt body. An
// unrelated closer body blocks the sweep and therefore cannot be converted into a mine hit by weapon-side logic.
[[nodiscard]] inline std::expected<std::optional<NavalMineDetonation>, std::string> AdvanceNavalMineAgainstSweep(
    const NavalMineDefinition& definition,
    NavalMineRuntimeState& runtime,
    const Physics::PhysicsBodyHandle targetBody,
    Physics::PhysicsBoxSweepQuery targetSweep,
    Physics::PhysicsWorld& physicsWorld,
    const double simulationTimeSeconds)
{
    const auto definitionValid = ValidateNavalMineDefinition(definition);
    if (!definitionValid)
    {
        return std::unexpected(definitionValid.error());
    }
    if (runtime.definitionId != definition.id || !targetBody.IsValid() ||
        !std::isfinite(simulationTimeSeconds) || simulationTimeSeconds < runtime.lastUpdateTimeSeconds)
    {
        return std::unexpected("naval mine runtime identity/target/time is invalid");
    }
    if (runtime.detonated)
    {
        runtime.lastUpdateTimeSeconds = simulationTimeSeconds;
        return std::optional<NavalMineDetonation>{};
    }
    if (!runtime.armed || !runtime.body.IsValid() || targetBody == runtime.body)
    {
        return std::unexpected("armed naval mine physical state is invalid");
    }
    if (targetSweep.ignoredBody.IsValid() && targetSweep.ignoredBody != targetBody)
    {
        return std::unexpected("naval mine sweep may ignore only its moving target body");
    }
    targetSweep.ignoredBody = targetBody;

    const auto hit = physicsWorld.SweepBoxClosest(targetSweep);
    if (!hit)
    {
        return std::unexpected("naval mine physical sweep failed: " + hit.error().message);
    }

    runtime.lastUpdateTimeSeconds = simulationTimeSeconds;
    if (!hit->has_value() || (**hit).body != runtime.body)
    {
        return std::optional<NavalMineDetonation>{};
    }

    Physics::PhysicsError destroyError;
    if (!physicsWorld.DestroyBody(runtime.body, &destroyError))
    {
        return std::unexpected("naval mine body consumption failed: " + destroyError.message);
    }
    runtime.body = {};
    runtime.armed = false;
    runtime.detonated = true;

    const Physics::PhysicsVector3 detonationPosition = (**hit).positionMeters;
    return std::optional<NavalMineDetonation>{NavalMineDetonation{
        .damage = Combat::CombatDamageEvent{
            .targetBody = targetBody,
            .positionMeters = detonationPosition,
            .damage = definition.contactDamage,
            .simulationTimeSeconds = simulationTimeSeconds},
        .explosion = Combat::CombatExplosionEvent{
            .positionMeters = detonationPosition,
            .nominalDamage = definition.contactDamage,
            .radiusMeters = definition.explosionRadiusMeters,
            .simulationTimeSeconds = simulationTimeSeconds}}};
}
} // namespace DeepRun::Weapons
