#pragma once

#include "Engine/Physics/PhysicsWorld.h"
#include "Game/Combat/SimpleDestroyerCombatController.h"
#include "Simulation/Acoustics/AcousticTypes.h"
#include "Simulation/Combat/CombatIntegrity.h"

#include <cmath>
#include <expected>
#include <optional>
#include <string>
#include <vector>

namespace DeepRun::Game::Combat
{
// M5-G bounded physical destroyer proxy for the Combat Playground. It deliberately proves composition rather
// than final surface-ship simulation: Jolt owns the physical body, only world X translation is permitted, and
// the authored body center stays at a fixed offset below the supplied mean surface. Full ship hydrodynamics,
// rolling/pitching, wakes and a production destroyer asset are later concerns.
struct SimpleDestroyerDefinition final
{
    std::string id;
    Physics::PhysicsVector3 collisionHalfExtentsMeters{45.0F, 4.0F, 5.0F};
    float massKilograms = 4'500'000.0F;
    float cruiseVelocityXMetersPerSecond = -8.0F;
    float bodyCenterBelowSurfaceMeters = 3.0F;
    float maximumIntegrity = 100.0F;

    Acoustics::AcousticSpectrum continuousSourceLevelDb{};
    std::string passiveSensorId{"M5_DESTROYER_PASSIVE_ARRAY"};
    Acoustics::AcousticSpectrum ambientNoiseLevelDb{};
    Acoustics::AcousticSpectrum selfNoiseLevelDb{};
    Acoustics::AcousticSpectrum sensitivityDb{};
    float minimumPeakSnrDb = 3.0F;

    Weapons::WeaponDefinition weapon{};
    SimpleDestroyerCombatConfig combat{};
};

struct SimpleDestroyerRuntimeState final
{
    std::string definitionId;
    Physics::PhysicsBodyHandle body{};
    DeepRun::Combat::CombatIntegrityState integrity{};
    Weapons::WeaponRuntimeState weapon{};
    SimpleDestroyerCombatState combatController{};
};

// Authoritative world-space source state is intentionally exposed only as a simulation-composition snapshot.
// Feeding this emitter through AcousticWorld still strips source identity before normal perception/AI consumers.
struct SimpleDestroyerAcousticSnapshot final
{
    Acoustics::AcousticEmitter emitter{};
    Acoustics::AcousticReceiver passiveReceiver{};
};

[[nodiscard]] inline std::expected<void, std::string> ValidateSimpleDestroyerDefinition(
    const SimpleDestroyerDefinition& definition)
{
    const auto weaponValid = Weapons::ValidateWeaponDefinition(definition.weapon);
    const auto combatValid = ValidateSimpleDestroyerCombatConfig(definition.combat);
    if (definition.id.empty() || !definition.collisionHalfExtentsMeters.IsFinite() ||
        definition.collisionHalfExtentsMeters.x <= 0.0F || definition.collisionHalfExtentsMeters.y <= 0.0F ||
        definition.collisionHalfExtentsMeters.z <= 0.0F || !std::isfinite(definition.massKilograms) ||
        definition.massKilograms <= 0.0F || !std::isfinite(definition.cruiseVelocityXMetersPerSecond) ||
        !std::isfinite(definition.bodyCenterBelowSurfaceMeters) ||
        definition.bodyCenterBelowSurfaceMeters < 0.0F || !std::isfinite(definition.maximumIntegrity) ||
        definition.maximumIntegrity <= 0.0F || !definition.continuousSourceLevelDb.IsFinite() ||
        definition.passiveSensorId.empty() || !definition.ambientNoiseLevelDb.IsFinite() ||
        !definition.selfNoiseLevelDb.IsFinite() || !definition.sensitivityDb.IsFinite() ||
        !std::isfinite(definition.minimumPeakSnrDb))
    {
        return std::unexpected("simple destroyer physical/acoustic definition is invalid");
    }
    if (!weaponValid)
    {
        return std::unexpected("simple destroyer weapon definition is invalid: " + weaponValid.error());
    }
    if (!combatValid)
    {
        return std::unexpected("simple destroyer combat definition is invalid: " + combatValid.error());
    }
    return {};
}

[[nodiscard]] inline std::expected<SimpleDestroyerRuntimeState, std::string> CreateSimpleDestroyerRuntime(
    const SimpleDestroyerDefinition& definition,
    Physics::PhysicsWorld& physicsWorld,
    const float surfaceLevelY,
    const float initialXMeters,
    const float gameplayPlaneZMeters,
    const double simulationTimeSeconds)
{
    const auto definitionValid = ValidateSimpleDestroyerDefinition(definition);
    if (!definitionValid)
    {
        return std::unexpected(definitionValid.error());
    }
    const float bodyCenterY = surfaceLevelY - definition.bodyCenterBelowSurfaceMeters;
    if (!std::isfinite(surfaceLevelY) || !std::isfinite(initialXMeters) || !std::isfinite(gameplayPlaneZMeters) ||
        !std::isfinite(bodyCenterY) || !std::isfinite(simulationTimeSeconds) || simulationTimeSeconds < 0.0)
    {
        return std::unexpected("simple destroyer creation input is invalid");
    }

    Physics::PhysicsDegreesOfFreedom cruiseDof;
    cruiseDof.translationX = true;
    cruiseDof.translationY = false;
    cruiseDof.translationZ = false;
    cruiseDof.rotationX = false;
    cruiseDof.rotationY = false;
    cruiseDof.rotationZ = false;

    Physics::PhysicsError physicsError;
    const Physics::PhysicsBodyHandle body = physicsWorld.CreateDynamicBoxBody(
        Physics::DynamicBoxBodyCreateInfo{
            .halfExtents = definition.collisionHalfExtentsMeters,
            .mass = definition.massKilograms,
            .position = {.x = initialXMeters, .y = bodyCenterY, .z = gameplayPlaneZMeters},
            .orientation = {},
            .gravityEnabled = false,
            .linearDamping = 0.0F,
            .angularDamping = 0.0F,
            .initialLinearVelocity = {.x = definition.cruiseVelocityXMetersPerSecond, .y = 0.0F, .z = 0.0F},
            .initialAngularVelocity = {},
            .degreesOfFreedom = cruiseDof},
        &physicsError);
    if (!body.IsValid())
    {
        return std::unexpected("simple destroyer Jolt body creation failed: " + physicsError.message);
    }

    const auto integrity = DeepRun::Combat::CreateCombatIntegrity(
        body, definition.maximumIntegrity, simulationTimeSeconds);
    const auto weapon = Weapons::CreateWeaponRuntime(definition.weapon, simulationTimeSeconds);
    if (!integrity || !weapon)
    {
        (void)physicsWorld.DestroyBody(body);
        return std::unexpected(!integrity
            ? "simple destroyer combat integrity creation failed: " + integrity.error()
            : "simple destroyer weapon runtime creation failed: " + weapon.error());
    }

    return SimpleDestroyerRuntimeState{
        .definitionId = definition.id,
        .body = body,
        .integrity = *integrity,
        .weapon = *weapon,
        .combatController = SimpleDestroyerCombatState{
            .selectedTrackId = std::nullopt,
            .lastUpdateTimeSeconds = simulationTimeSeconds}};
}

[[nodiscard]] inline std::expected<SimpleDestroyerAcousticSnapshot, std::string> SampleSimpleDestroyerAcoustics(
    const SimpleDestroyerDefinition& definition,
    const SimpleDestroyerRuntimeState& runtime,
    const Physics::PhysicsWorld& physicsWorld)
{
    const auto definitionValid = ValidateSimpleDestroyerDefinition(definition);
    if (!definitionValid)
    {
        return std::unexpected(definitionValid.error());
    }
    if (runtime.definitionId != definition.id || !runtime.body.IsValid() || runtime.integrity.body != runtime.body ||
        runtime.weapon.definitionId != definition.weapon.id)
    {
        return std::unexpected("simple destroyer runtime does not match its definition/body authority");
    }

    const auto bodyState = physicsWorld.GetBodyState(runtime.body);
    if (!bodyState || !bodyState->position.IsFinite() || !bodyState->linearVelocity.IsFinite())
    {
        return std::unexpected("simple destroyer physical body state is unavailable");
    }

    return SimpleDestroyerAcousticSnapshot{
        .emitter = Acoustics::AcousticEmitter{
            .positionMeters = bodyState->position,
            .velocityMetersPerSecond = bodyState->linearVelocity,
            .continuousSourceLevelDb = definition.continuousSourceLevelDb},
        .passiveReceiver = Acoustics::AcousticReceiver{
            .sensorId = definition.passiveSensorId,
            .positionMeters = bodyState->position,
            .ambientNoiseLevelDb = definition.ambientNoiseLevelDb,
            .selfNoiseLevelDb = definition.selfNoiseLevelDb,
            .sensitivityDb = definition.sensitivityDb,
            .minimumPeakSnrDb = definition.minimumPeakSnrDb}};
}

[[nodiscard]] inline std::expected<SimpleDestroyerCombatDecision, std::string> AdvanceSimpleDestroyerCombatRuntime(
    const SimpleDestroyerDefinition& definition,
    SimpleDestroyerRuntimeState& runtime,
    const std::vector<Perception::Track>& perceivedTracks,
    const double simulationTimeSeconds)
{
    if (runtime.definitionId != definition.id || runtime.integrity.body != runtime.body)
    {
        return std::unexpected("simple destroyer runtime identity is invalid");
    }
    if (runtime.integrity.destroyed)
    {
        // A destroyed surface actor is a valid terminal gameplay state, not a frame error.
        // Keep the physical/damage authority available for presentation and post-impact acceptance,
        // but freeze its weapon/controller behavior so it cannot acquire, prepare, or launch again.
        if (!std::isfinite(simulationTimeSeconds) ||
            simulationTimeSeconds < runtime.combatController.lastUpdateTimeSeconds ||
            simulationTimeSeconds < runtime.weapon.lastUpdateTimeSeconds)
        {
            return std::unexpected("destroyed simple destroyer combat state is invalid or time-reversing");
        }
        runtime.combatController.selectedTrackId.reset();
        runtime.combatController.lastUpdateTimeSeconds = simulationTimeSeconds;
        return SimpleDestroyerCombatDecision{
            .action = SimpleDestroyerCombatAction::Hold,
            .perceivedTrackId = std::nullopt};
    }
    return AdvanceSimpleDestroyerCombat(
        definition.combat,
        definition.weapon,
        runtime.combatController,
        runtime.weapon,
        perceivedTracks,
        simulationTimeSeconds);
}

[[nodiscard]] inline std::expected<void, std::string> ApplySimpleDestroyerDamage(
    const SimpleDestroyerDefinition& definition,
    SimpleDestroyerRuntimeState& runtime,
    const DeepRun::Combat::CombatDamageEvent& event)
{
    if (runtime.definitionId != definition.id || runtime.integrity.body != runtime.body)
    {
        return std::unexpected("simple destroyer runtime identity is invalid");
    }
    return DeepRun::Combat::ApplyCombatDamage(runtime.integrity, event);
}
} // namespace DeepRun::Game::Combat
