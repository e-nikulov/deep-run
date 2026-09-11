#pragma once

#include "Game/Combat/CombatPlaygroundPresentation.h"
#include "Game/Combat/CombatPlaygroundRuntime.h"
#include "Game/Submarine/AnteyAcousticModel.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace DeepRun::Tests
{
[[nodiscard]] inline float M5J4AngleError(const float first, const float second) noexcept
{
    return std::abs(std::remainder(first - second, 6.2831853F));
}

[[nodiscard]] inline bool RunM5PlayerDefensiveDecoyChecks(Physics::PhysicsWorld& physicsWorld)
{
    using namespace Game::Combat;

    if (!physicsWorld.IsInitialized())
    {
        return false;
    }
    auto runtimeResult = CombatPlaygroundRuntime::Create(physicsWorld, 0.0F, 0.0);
    if (!runtimeResult)
    {
        return false;
    }
    auto runtime = std::move(*runtimeResult);

    const auto playerSnapshotResult = Game::Submarine::BuildAnteyAcousticSnapshot(
        Game::Submarine::AnteyAcousticRuntimeState{
            .bodyReferencePositionMeters = {.x = 0.0F, .y = -100.0F, .z = 0.0F},
            .linearVelocityMetersPerSecond = {},
            .shaftRpm = 35.0F,
            .signedDepthMeters = 100.0F},
        Acoustics::AcousticSpectrum{.levelDb = {43.0F, 41.0F, 39.0F, 37.0F}});
    if (!playerSnapshotResult)
    {
        return false;
    }
    const auto playerSnapshot = *playerSnapshotResult;

    const Physics::PhysicsVector3 playerHalfExtentsMeters{.x = 75.0F, .y = 8.0F, .z = 8.0F};
    const auto playerBody = physicsWorld.CreateDynamicBoxBody(Physics::DynamicBoxBodyCreateInfo{
        .halfExtents = playerHalfExtentsMeters,
        .mass = 12'000'000.0F,
        .position = playerSnapshot.emitter.positionMeters,
        .orientation = {},
        .gravityEnabled = false,
        .linearDamping = 0.0F,
        .angularDamping = 0.0F,
        .initialLinearVelocity = {},
        .initialAngularVelocity = {}});
    if (!playerBody.IsValid())
    {
        return false;
    }
    const auto bound = runtime.BindPlayerPhysicalProxy(
        Game::Submarine::AnteyPhysicalCollisionProxySnapshot{
            .body = playerBody,
            .positionMeters = playerSnapshot.emitter.positionMeters,
            .orientation = {},
            .halfExtentsMeters = playerHalfExtentsMeters},
        playerSnapshot,
        0.0);
    if (!bound || !runtime.PlayerDecoyAvailable() || runtime.PlayerDecoy().has_value())
    {
        return false;
    }

    constexpr float fixedDeltaSeconds = 1.0F / 60.0F;
    double simulationTimeSeconds = 0.0;
    bool threatDetected = false;
    for (int tick = 0; tick < 2400; ++tick)
    {
        simulationTimeSeconds = static_cast<double>(tick) * fixedDeltaSeconds;
        const auto frame = runtime.AdvancePlayerControlled(playerSnapshot, {}, simulationTimeSeconds);
        if (!frame)
        {
            return false;
        }
        physicsWorld.Step(fixedDeltaSeconds);
        if (frame->playerCombat.incomingThreatDetected)
        {
            if (!runtime.DestroyerTorpedo() || !frame->playerCombat.canDeployDecoy ||
                frame->playerCombat.playerDecoyActive)
            {
                return false;
            }
            threatDetected = true;
            break;
        }
    }
    if (!threatDetected)
    {
        return false;
    }

    simulationTimeSeconds += fixedDeltaSeconds;
    const std::array deploy{
        PlayerCombatCommand{.type = PlayerCombatCommandType::DeployDecoy}};
    const auto deployed = runtime.AdvancePlayerControlled(playerSnapshot, deploy, simulationTimeSeconds);
    if (!deployed || !deployed->playerCombat.lastCommand || !deployed->playerCombat.lastCommand->accepted ||
        deployed->playerCombat.lastCommand->command != PlayerCombatCommandType::DeployDecoy ||
        deployed->playerCombat.canDeployDecoy || !deployed->playerCombat.playerDecoyActive ||
        runtime.PlayerDecoyAvailable() || !runtime.PlayerDecoy() || !runtime.PlayerDecoy()->active)
    {
        return false;
    }

    const auto presentation = BuildCombatPlaygroundPresentationSnapshot(runtime, physicsWorld, simulationTimeSeconds);
    if (!presentation || !presentation->playerDecoy || !presentation->playerDecoy->active ||
        presentation->playerDecoy->positionMeters != runtime.PlayerDecoy()->emitter.positionMeters)
    {
        return false;
    }
    const auto draws = BuildCombatPlaygroundPresentationDraws(*presentation);
    if (!draws || !std::ranges::any_of(*draws, [](const auto& draw) {
            return draw.element == CombatPlaygroundPresentationElement::PlayerAcousticDecoy;
        }))
    {
        return false;
    }
    physicsWorld.Step(fixedDeltaSeconds);

    const double originalDeploymentTime = runtime.PlayerDecoy()->deploymentTimeSeconds;
    simulationTimeSeconds += fixedDeltaSeconds;
    const auto repeated = runtime.AdvancePlayerControlled(playerSnapshot, deploy, simulationTimeSeconds);
    if (!repeated || !repeated->playerCombat.lastCommand || repeated->playerCombat.lastCommand->accepted ||
        repeated->playerCombat.lastCommand->command != PlayerCombatCommandType::DeployDecoy ||
        !runtime.PlayerDecoy() || runtime.PlayerDecoy()->deploymentTimeSeconds != originalDeploymentTime)
    {
        return false;
    }
    physicsWorld.Step(fixedDeltaSeconds);

    bool sawSeekerSelection = false;
    bool sawDiversionTowardDecoy = false;
    bool sawExpiredDecoy = false;
    for (int tick = 0; tick < 1200; ++tick)
    {
        simulationTimeSeconds += fixedDeltaSeconds;
        const auto frame = runtime.AdvancePlayerControlled(playerSnapshot, {}, simulationTimeSeconds);
        if (!frame)
        {
            return false;
        }
        if (runtime.DestroyerTorpedo() &&
            runtime.DestroyerTorpedo()->movementDomain == Weapons::MovementDomain::Underwater &&
            runtime.PlayerDecoy() && runtime.PlayerDecoy()->active &&
            runtime.DestroyerTorpedoSeekerState().selectedTrackId)
        {
            sawSeekerSelection = true;
            const auto& torpedo = *runtime.DestroyerTorpedo();
            const auto& decoy = *runtime.PlayerDecoy();
            const float playerBearing = static_cast<float>(std::atan2(
                static_cast<double>(playerSnapshot.emitter.positionMeters.y - torpedo.positionMeters.y),
                static_cast<double>(playerSnapshot.emitter.positionMeters.x - torpedo.positionMeters.x)));
            const float decoyBearing = static_cast<float>(std::atan2(
                static_cast<double>(decoy.emitter.positionMeters.y - torpedo.positionMeters.y),
                static_cast<double>(decoy.emitter.positionMeters.x - torpedo.positionMeters.x)));
            if (M5J4AngleError(torpedo.headingRadians, decoyBearing) + 0.02F <
                M5J4AngleError(torpedo.headingRadians, playerBearing))
            {
                sawDiversionTowardDecoy = true;
            }
        }
        if (runtime.PlayerDecoy() && !runtime.PlayerDecoy()->active)
        {
            if (frame->playerCombat.canDeployDecoy || frame->playerCombat.playerDecoyActive)
            {
                return false;
            }
            sawExpiredDecoy = true;
        }
        physicsWorld.Step(fixedDeltaSeconds);
        if (sawDiversionTowardDecoy && sawExpiredDecoy)
        {
            break;
        }
    }

    const auto mineBody = runtime.Mine() ? runtime.Mine()->body : Physics::PhysicsBodyHandle{};
    const bool destroyedMine = !mineBody.IsValid() || physicsWorld.DestroyBody(mineBody);
    const bool destroyedPlayer = physicsWorld.DestroyBody(playerBody);
    const bool destroyedDestroyer = physicsWorld.DestroyBody(runtime.Destroyer().body);
    return sawSeekerSelection && sawDiversionTowardDecoy && sawExpiredDecoy &&
        destroyedMine && destroyedPlayer && destroyedDestroyer;
}
} // namespace DeepRun::Tests
