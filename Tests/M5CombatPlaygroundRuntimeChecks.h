#pragma once

#include "Game/Combat/CombatPlaygroundRuntime.h"
#include "Game/Submarine/AnteyAcousticModel.h"

#include <cmath>

namespace DeepRun::Tests
{
[[nodiscard]] inline bool RunM5CombatPlaygroundRuntimeChecks(Physics::PhysicsWorld& physicsWorld)
{
    if (!physicsWorld.IsInitialized())
    {
        return false;
    }

    const auto runtimeResult = Game::Combat::CombatPlaygroundRuntime::Create(physicsWorld, 0.0F, 0.0);
    if (!runtimeResult)
    {
        return false;
    }
    auto runtime = *runtimeResult;

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

    bool sawPlayerSpatialTrack = false;
    bool sawDestroyerAwareness = false;
    bool sawDestroyerPreparation = false;
    bool sawTorpedo = false;
    bool sawDecoy = false;
    bool sawImpact = false;

    constexpr float fixedDeltaSeconds = 1.0F / 60.0F;
    for (int tick = 0; tick <= 240; ++tick)
    {
        const double simulationTimeSeconds = static_cast<double>(tick) * fixedDeltaSeconds;
        const auto frame = runtime.Advance(playerSnapshot, simulationTimeSeconds);
        if (!frame)
        {
            return false;
        }

        for (const auto& track : frame->playerTracks)
        {
            sawPlayerSpatialTrack = sawPlayerSpatialTrack ||
                (track.lifecycle == Perception::TrackLifecycleState::Confirmed &&
                 track.estimatedPositionMeters.has_value() && track.positionUncertaintyMeters.has_value());
        }
        for (const auto& track : frame->destroyerTracks)
        {
            sawDestroyerAwareness = sawDestroyerAwareness ||
                track.lifecycle == Perception::TrackLifecycleState::Confirmed;
            // Passive destroyer awareness must remain bearing-only in this live composition.
            if (track.estimatedPositionMeters.has_value())
            {
                return false;
            }
        }
        sawDestroyerPreparation = sawDestroyerPreparation ||
            frame->destroyerDecision.action == Game::Combat::SimpleDestroyerCombatAction::PrepareWeapon;
        sawTorpedo = sawTorpedo || runtime.PlayerTorpedo().has_value();
        sawDecoy = sawDecoy || (runtime.Decoy().has_value() && runtime.Decoy()->active);

        if (frame->playerTorpedoImpact)
        {
            if (frame->playerTorpedoImpact->physicsHit.body != runtime.Destroyer().body ||
                !runtime.LastExplosion().has_value())
            {
                return false;
            }
            sawImpact = true;
        }

        physicsWorld.Step(fixedDeltaSeconds);
    }

    const auto destroyerState = physicsWorld.GetBodyState(runtime.Destroyer().body);
    if (!sawPlayerSpatialTrack || !sawDestroyerAwareness || !sawDestroyerPreparation || !sawTorpedo ||
        !sawDecoy || !sawImpact || !destroyerState ||
        runtime.Destroyer().weapon.phase == Weapons::WeaponPhase::Launched ||
        runtime.Destroyer().integrity.destroyed ||
        std::abs(runtime.Destroyer().integrity.remainingIntegrity - 40.0F) > 0.001F ||
        !runtime.PlayerTorpedo() || runtime.PlayerTorpedo()->movementDomain != Weapons::MovementDomain::Spent)
    {
        return false;
    }

    // The destroyer AI heard and reacted to the player but never received a ranged target solution, so it must
    // not launch merely because the scenario simulator knows where the player is.
    if (runtime.Destroyer().weapon.targetTrackId.has_value())
    {
        return false;
    }

    return physicsWorld.DestroyBody(runtime.Destroyer().body);
}
} // namespace DeepRun::Tests
