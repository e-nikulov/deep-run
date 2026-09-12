#pragma once

#include "Game/Combat/CombatPlaygroundRuntime.h"
#include "Game/Submarine/AnteyAcousticModel.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace DeepRun::Tests
{
[[nodiscard]] inline bool RunM5PlayerControlledCombatChecks(Physics::PhysicsWorld& physicsWorld)
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

    // Weapon-employment gating consumes the same authoritative ownship physical snapshot as windowed play.
    // Keep this direct-runtime test honest by binding a real PhysicsWorld body instead of bypassing the gate.
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
    const auto boundPlayer = runtime.BindPlayerPhysicalProxy(
        Game::Submarine::AnteyPhysicalCollisionProxySnapshot{
            .body = playerBody,
            .positionMeters = playerSnapshot.emitter.positionMeters,
            .orientation = {},
            .halfExtentsMeters = playerHalfExtentsMeters},
        playerSnapshot,
        0.0);
    if (!boundPlayer)
    {
        (void)physicsWorld.DestroyBody(playerBody);
        return false;
    }

    constexpr float fixedDeltaSeconds = 1.0F / 60.0F;
    double commandTimeSeconds = 0.0;
    bool sawPassiveBearingOnlyTrack = false;

    // Normal play must not auto-range. Wait only for the continuous passive destroyer contact and require that
    // it remains bearing-only and therefore insufficient for the position-requiring heavyweight weapon.
    for (int tick = 0; tick <= 180; ++tick)
    {
        commandTimeSeconds = static_cast<double>(tick) * fixedDeltaSeconds;
        const auto frame = runtime.AdvancePlayerControlled(playerSnapshot, {}, commandTimeSeconds);
        if (!frame || frame->playerCombat.selectedTrackId || runtime.PlayerTorpedo() ||
            frame->playerCombat.canActiveSonarPing || frame->playerCombat.activeSonarPulsePending)
        {
            return false;
        }
        for (const auto& track : frame->playerTracks)
        {
            if (track.lifecycle == Perception::TrackLifecycleState::Confirmed)
            {
                if (track.estimatedPositionMeters || track.positionUncertaintyMeters ||
                    Weapons::ValidateTrackForWeapon(runtime.PlayerCombat().WeaponDefinition(), track))
                {
                    return false;
                }
                sawPassiveBearingOnlyTrack = true;
            }
        }
        physicsWorld.Step(fixedDeltaSeconds);
        if (sawPassiveBearingOnlyTrack)
        {
            break;
        }
    }
    if (!sawPassiveBearingOnlyTrack || runtime.PlayerCombat().Weapon().phase != Weapons::WeaponPhase::Stored)
    {
        return false;
    }

    const std::array ping{
        PlayerCombatCommand{.type = PlayerCombatCommandType::ActiveSonarPing}};
    commandTimeSeconds += fixedDeltaSeconds;
    const auto unselectedPing = runtime.AdvancePlayerControlled(playerSnapshot, ping, commandTimeSeconds);
    if (!unselectedPing || !unselectedPing->playerCombat.lastCommand ||
        unselectedPing->playerCombat.lastCommand->accepted ||
        unselectedPing->playerCombat.lastCommand->command != PlayerCombatCommandType::ActiveSonarPing ||
        unselectedPing->playerCombat.activeSonarPulsePending || runtime.PlayerActivePulse())
    {
        return false;
    }
    physicsWorld.Step(fixedDeltaSeconds);

    const std::array select{
        PlayerCombatCommand{.type = PlayerCombatCommandType::SelectNextTrack}};
    commandTimeSeconds += fixedDeltaSeconds;
    const auto selected = runtime.AdvancePlayerControlled(playerSnapshot, select, commandTimeSeconds);
    if (!selected || !selected->playerCombat.selectedTrackId || !selected->playerCombat.canActiveSonarPing ||
        selected->playerCombat.selectedTrackHasEstimatedPosition || !selected->playerCombat.lastCommand ||
        !selected->playerCombat.lastCommand->accepted)
    {
        return false;
    }
    const auto selectedTrackId = *selected->playerCombat.selectedTrackId;
    const auto selectedTrack = std::ranges::find_if(selected->playerTracks, [selectedTrackId](const auto& track) {
        return track.trackId == selectedTrackId;
    });
    if (selectedTrack == selected->playerTracks.end())
    {
        return false;
    }
    const float selectedBearing = selectedTrack->estimatedBearingRadians;
    physicsWorld.Step(fixedDeltaSeconds);

    commandTimeSeconds += fixedDeltaSeconds;
    const double pingEmissionTimeSeconds = commandTimeSeconds;
    const auto emitted = runtime.AdvancePlayerControlled(playerSnapshot, ping, commandTimeSeconds);
    if (!emitted || !emitted->playerCombat.lastCommand || !emitted->playerCombat.lastCommand->accepted ||
        emitted->playerCombat.lastCommand->command != PlayerCombatCommandType::ActiveSonarPing ||
        emitted->playerCombat.lastCommand->trackId != std::optional<std::uint64_t>{selectedTrackId} ||
        !emitted->playerCombat.activeSonarPulsePending || emitted->playerCombat.canActiveSonarPing ||
        emitted->playerCombat.selectedTrackHasEstimatedPosition || !runtime.PlayerActivePulse())
    {
        return false;
    }
    const auto& pulse = *runtime.PlayerActivePulse();
    const float emittedBearing = static_cast<float>(std::atan2(
        static_cast<double>(pulse.forwardUnitVector.y), static_cast<double>(pulse.forwardUnitVector.x)));
    if (std::abs(std::remainder(emittedBearing - selectedBearing, 6.2831853F)) > 0.001F)
    {
        return false;
    }
    physicsWorld.Step(fixedDeltaSeconds);

    // A second edge while the round trip is pending is a normal rejection, not a second pulse.
    commandTimeSeconds += fixedDeltaSeconds;
    const auto repeatedPing = runtime.AdvancePlayerControlled(playerSnapshot, ping, commandTimeSeconds);
    if (!repeatedPing || !repeatedPing->playerCombat.lastCommand ||
        repeatedPing->playerCombat.lastCommand->accepted ||
        !repeatedPing->playerCombat.activeSonarPulsePending || !runtime.PlayerActivePulse())
    {
        return false;
    }
    physicsWorld.Step(fixedDeltaSeconds);

    bool sawQualifiedSpatialTrack = false;
    for (int tick = 0; tick < 240; ++tick)
    {
        commandTimeSeconds += fixedDeltaSeconds;
        const auto frame = runtime.AdvancePlayerControlled(playerSnapshot, {}, commandTimeSeconds);
        if (!frame || runtime.PlayerTorpedo())
        {
            return false;
        }
        for (const auto& track : frame->playerTracks)
        {
            if (track.trackId == selectedTrackId &&
                Weapons::ValidateTrackForWeapon(runtime.PlayerCombat().WeaponDefinition(), track))
            {
                if (!track.estimatedPositionMeters || !track.positionUncertaintyMeters ||
                    commandTimeSeconds - pingEmissionTimeSeconds < 1.5)
                {
                    return false;
                }
                sawQualifiedSpatialTrack = true;
                break;
            }
        }
        physicsWorld.Step(fixedDeltaSeconds);
        if (sawQualifiedSpatialTrack)
        {
            break;
        }
    }
    if (!sawQualifiedSpatialTrack || runtime.PlayerCombat().Weapon().phase != Weapons::WeaponPhase::Stored)
    {
        return false;
    }

    // Fire without readiness remains a normal commander rejection even after a valid ranged solution exists.
    const std::array fire{
        PlayerCombatCommand{.type = PlayerCombatCommandType::FireWeapon}};
    commandTimeSeconds += fixedDeltaSeconds;
    const auto rejectedFire = runtime.AdvancePlayerControlled(playerSnapshot, fire, commandTimeSeconds);
    if (!rejectedFire || !rejectedFire->playerCombat.lastCommand || rejectedFire->playerCombat.lastCommand->accepted ||
        runtime.PlayerTorpedo() || runtime.PlayerCombat().Weapon().targetTrackId)
    {
        return false;
    }
    physicsWorld.Step(fixedDeltaSeconds);

    const std::array prepare{
        PlayerCombatCommand{.type = PlayerCombatCommandType::PrepareWeapon}};
    commandTimeSeconds += fixedDeltaSeconds;
    const auto preparing = runtime.AdvancePlayerControlled(playerSnapshot, prepare, commandTimeSeconds);
    if (!preparing || preparing->playerCombat.weaponPhase != Weapons::WeaponPhase::Preparing ||
        !preparing->playerCombat.selectedTrackId || preparing->playerCombat.canFireWeapon ||
        !preparing->playerCombat.lastCommand || !preparing->playerCombat.lastCommand->accepted ||
        preparing->playerCombat.lastCommand->command != PlayerCombatCommandType::PrepareWeapon || runtime.PlayerTorpedo())
    {
        return false;
    }
    physicsWorld.Step(fixedDeltaSeconds);

    commandTimeSeconds += fixedDeltaSeconds;
    const auto earlyFire = runtime.AdvancePlayerControlled(playerSnapshot, fire, commandTimeSeconds);
    if (!earlyFire || !earlyFire->playerCombat.lastCommand || earlyFire->playerCombat.lastCommand->accepted ||
        earlyFire->playerCombat.weaponPhase != Weapons::WeaponPhase::Preparing ||
        runtime.PlayerCombat().Weapon().targetTrackId || runtime.PlayerTorpedo())
    {
        return false;
    }
    physicsWorld.Step(fixedDeltaSeconds);

    bool sawReady = false;
    for (int tick = 0; tick < 90; ++tick)
    {
        commandTimeSeconds += fixedDeltaSeconds;
        const auto frame = runtime.AdvancePlayerControlled(playerSnapshot, {}, commandTimeSeconds);
        if (!frame || runtime.PlayerTorpedo())
        {
            return false;
        }
        physicsWorld.Step(fixedDeltaSeconds);
        if (frame->playerCombat.weaponPhase == Weapons::WeaponPhase::Ready)
        {
            if (!frame->playerCombat.canFireWeapon || !frame->playerCombat.selectedTrackWeaponQualified)
            {
                return false;
            }
            sawReady = true;
            break;
        }
    }
    if (!sawReady)
    {
        return false;
    }

    commandTimeSeconds += fixedDeltaSeconds;
    const auto launched = runtime.AdvancePlayerControlled(playerSnapshot, fire, commandTimeSeconds);
    if (!launched || !launched->playerCombat.lastCommand || !launched->playerCombat.lastCommand->accepted ||
        launched->playerCombat.lastCommand->command != PlayerCombatCommandType::FireWeapon ||
        launched->playerCombat.weaponPhase != Weapons::WeaponPhase::Launched ||
        !launched->playerCombat.weaponTargetTrackId ||
        launched->playerCombat.weaponTargetTrackId != launched->playerCombat.selectedTrackId ||
        !runtime.PlayerTorpedo() || !runtime.Decoy() || !runtime.Decoy()->active)
    {
        return false;
    }
    if (runtime.PlayerTorpedo()->guidanceTrackId != *launched->playerCombat.selectedTrackId ||
        runtime.PlayerTorpedo()->impactedBody)
    {
        return false;
    }

    const auto mineBody = runtime.Mine() ? runtime.Mine()->body : Physics::PhysicsBodyHandle{};
    const bool destroyedMine = !mineBody.IsValid() || physicsWorld.DestroyBody(mineBody);
    const bool destroyedPlayer = physicsWorld.DestroyBody(playerBody);
    const bool destroyedDestroyer = physicsWorld.DestroyBody(runtime.Destroyer().body);
    return destroyedMine && destroyedPlayer && destroyedDestroyer;
}
} // namespace DeepRun::Tests