#pragma once

#include "Game/Combat/CombatPlaygroundRuntime.h"
#include "Game/Submarine/AnteyAcousticModel.h"

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

    constexpr float fixedDeltaSeconds = 1.0F / 60.0F;
    bool sawQualifiedTrackWithoutAutoLaunch = false;
    double commandTimeSeconds = 0.0;

    for (int tick = 0; tick <= 360; ++tick)
    {
        const double simulationTimeSeconds = static_cast<double>(tick) * fixedDeltaSeconds;
        const auto frame = runtime.AdvancePlayerControlled(playerSnapshot, {}, simulationTimeSeconds);
        if (!frame)
        {
            return false;
        }

        if (frame->playerCombat.selectedTrackId.has_value() || runtime.PlayerTorpedo().has_value())
        {
            return false;
        }

        for (const auto& track : frame->playerTracks)
        {
            if (Weapons::ValidateTrackForWeapon(runtime.PlayerCombat().WeaponDefinition(), track))
            {
                sawQualifiedTrackWithoutAutoLaunch = true;
                commandTimeSeconds = simulationTimeSeconds + fixedDeltaSeconds;
                break;
            }
        }
        physicsWorld.Step(fixedDeltaSeconds);
        if (sawQualifiedTrackWithoutAutoLaunch)
        {
            break;
        }
    }

    if (!sawQualifiedTrackWithoutAutoLaunch || runtime.PlayerCombat().Weapon().phase != Weapons::WeaponPhase::Stored)
    {
        return false;
    }

    // Fire without a selected track is a normal commander rejection and must not create physical weapon state.
    const std::array rejectedFire{
        PlayerCombatCommand{.type = PlayerCombatCommandType::FireWeapon}};
    const auto rejected = runtime.AdvancePlayerControlled(playerSnapshot, rejectedFire, commandTimeSeconds);
    if (!rejected || !rejected->playerCombat.lastCommand || rejected->playerCombat.lastCommand->accepted ||
        runtime.PlayerTorpedo().has_value() || runtime.PlayerCombat().Weapon().targetTrackId.has_value())
    {
        return false;
    }
    physicsWorld.Step(fixedDeltaSeconds);

    commandTimeSeconds += fixedDeltaSeconds;
    // The zero-second M5 playground preparation profile allows the canonical command sequence on one fixed tick.
    const std::array commands{
        PlayerCombatCommand{.type = PlayerCombatCommandType::SelectNextTrack},
        PlayerCombatCommand{.type = PlayerCombatCommandType::PrepareWeapon},
        PlayerCombatCommand{.type = PlayerCombatCommandType::FireWeapon}};
    const auto launched = runtime.AdvancePlayerControlled(playerSnapshot, commands, commandTimeSeconds);
    if (!launched || !launched->playerCombat.lastCommand || !launched->playerCombat.lastCommand->accepted ||
        launched->playerCombat.lastCommand->command != PlayerCombatCommandType::FireWeapon ||
        launched->playerCombat.weaponPhase != Weapons::WeaponPhase::Launched ||
        !launched->playerCombat.weaponTargetTrackId.has_value() ||
        !launched->playerCombat.selectedTrackId.has_value() ||
        launched->playerCombat.weaponTargetTrackId != launched->playerCombat.selectedTrackId ||
        !runtime.PlayerTorpedo().has_value() || !runtime.Decoy().has_value() || !runtime.Decoy()->active)
    {
        return false;
    }

    // Launch identity remains perceived-world track identity. Physical body authority is still absent until Jolt
    // reports a future impact.
    if (runtime.PlayerTorpedo()->guidanceTrackId != *launched->playerCombat.selectedTrackId ||
        runtime.PlayerTorpedo()->impactedBody.has_value())
    {
        return false;
    }

    return physicsWorld.DestroyBody(runtime.Destroyer().body);
}
} // namespace DeepRun::Tests
