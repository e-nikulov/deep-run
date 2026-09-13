#pragma once

#include "Engine/Physics/PhysicsWorld.h"
#include "Simulation/Acoustics/AcousticTypes.h"
#include "Simulation/Combat/CombatIntegrity.h"

#include <cmath>
#include <expected>
#include <string>

namespace DeepRun::Game::Combat
{
// Bounded civilian surface-contact runtime for the periscope/ROE gameplay slice. It intentionally has no
// weapon, target-selection or combat-AI state. Jolt owns the body; AcousticWorld consumes the emitter; normal
// commander code sees only perceived observations. Values are gameplay tuning, not a named real vessel model.
struct SimpleCivilianVesselDefinition final
{
    std::string id{"m5.live-civilian-vessel"};
    Physics::PhysicsVector3 collisionHalfExtentsMeters{35.0F, 4.0F, 5.0F};
    float massKilograms = 3'500'000.0F;
    float cruiseVelocityXMetersPerSecond = 1.25F;
    float bodyCenterBelowSurfaceMeters = 2.5F;
    float maximumIntegrity = 100.0F;
    Acoustics::AcousticSpectrum continuousSourceLevelDb{.levelDb = {139.0F, 136.0F, 132.0F, 126.0F}};
};

struct SimpleCivilianVesselRuntimeState final
{
    std::string definitionId;
    Physics::PhysicsBodyHandle body{};
    DeepRun::Combat::CombatIntegrityState integrity{};
};

[[nodiscard]] inline std::expected<SimpleCivilianVesselRuntimeState, std::string> CreateSimpleCivilianVesselRuntime(
    const SimpleCivilianVesselDefinition& definition,
    Physics::PhysicsWorld& physicsWorld,
    const float surfaceLevelY,
    const float initialXMeters,
    const float gameplayPlaneZMeters,
    const double simulationTimeSeconds)
{
    if (!physicsWorld.IsInitialized() || definition.id.empty() || !definition.collisionHalfExtentsMeters.IsFinite() ||
        definition.collisionHalfExtentsMeters.x <= 0.0F || definition.collisionHalfExtentsMeters.y <= 0.0F ||
        definition.collisionHalfExtentsMeters.z <= 0.0F || !std::isfinite(definition.massKilograms) ||
        definition.massKilograms <= 0.0F || !std::isfinite(definition.cruiseVelocityXMetersPerSecond) ||
        !std::isfinite(definition.bodyCenterBelowSurfaceMeters) || definition.bodyCenterBelowSurfaceMeters < 0.0F ||
        !std::isfinite(definition.maximumIntegrity) || definition.maximumIntegrity <= 0.0F ||
        !definition.continuousSourceLevelDb.IsFinite() || !std::isfinite(surfaceLevelY) ||
        !std::isfinite(initialXMeters) || !std::isfinite(gameplayPlaneZMeters) ||
        !std::isfinite(simulationTimeSeconds) || simulationTimeSeconds < 0.0)
    {
        return std::unexpected("civilian vessel definition/creation input is invalid");
    }

    Physics::PhysicsDegreesOfFreedom dof;
    dof.translationX = true;
    dof.translationY = false;
    dof.translationZ = false;
    dof.rotationX = false;
    dof.rotationY = false;
    dof.rotationZ = false;

    Physics::PhysicsError error;
    const Physics::PhysicsBodyHandle body = physicsWorld.CreateDynamicBoxBody(
        Physics::DynamicBoxBodyCreateInfo{
            .halfExtents = definition.collisionHalfExtentsMeters,
            .mass = definition.massKilograms,
            .position = {.x = initialXMeters,
                         .y = surfaceLevelY - definition.bodyCenterBelowSurfaceMeters,
                         .z = gameplayPlaneZMeters},
            .orientation = {},
            .gravityEnabled = false,
            .linearDamping = 0.0F,
            .angularDamping = 0.0F,
            .initialLinearVelocity = {.x = definition.cruiseVelocityXMetersPerSecond, .y = 0.0F, .z = 0.0F},
            .initialAngularVelocity = {},
            .degreesOfFreedom = dof},
        &error);
    if (!body.IsValid())
    {
        return std::unexpected("civilian vessel Jolt body creation failed: " + error.message);
    }

    const auto integrity = DeepRun::Combat::CreateCombatIntegrity(body, definition.maximumIntegrity, simulationTimeSeconds);
    if (!integrity)
    {
        (void)physicsWorld.DestroyBody(body);
        return std::unexpected("civilian vessel integrity creation failed: " + integrity.error());
    }
    return SimpleCivilianVesselRuntimeState{
        .definitionId = definition.id,
        .body = body,
        .integrity = *integrity};
}

[[nodiscard]] inline std::expected<Acoustics::AcousticEmitter, std::string> SampleSimpleCivilianVesselEmitter(
    const SimpleCivilianVesselDefinition& definition,
    const SimpleCivilianVesselRuntimeState& runtime,
    const Physics::PhysicsWorld& physicsWorld)
{
    if (runtime.definitionId != definition.id || !runtime.body.IsValid() || runtime.integrity.body != runtime.body)
    {
        return std::unexpected("civilian vessel runtime identity is invalid");
    }
    const auto state = physicsWorld.GetBodyState(runtime.body);
    if (!state || !state->position.IsFinite() || !state->linearVelocity.IsFinite())
    {
        return std::unexpected("civilian vessel physical state is unavailable");
    }
    return Acoustics::AcousticEmitter{
        .positionMeters = state->position,
        .velocityMetersPerSecond = state->linearVelocity,
        .continuousSourceLevelDb = definition.continuousSourceLevelDb};
}

[[nodiscard]] inline std::expected<void, std::string> ApplySimpleCivilianVesselDamage(
    const SimpleCivilianVesselDefinition& definition,
    SimpleCivilianVesselRuntimeState& runtime,
    const DeepRun::Combat::CombatDamageEvent& event)
{
    if (runtime.definitionId != definition.id || runtime.integrity.body != runtime.body)
    {
        return std::unexpected("civilian vessel runtime identity is invalid");
    }
    return DeepRun::Combat::ApplyCombatDamage(runtime.integrity, event);
}
} // namespace DeepRun::Game::Combat
