#pragma once

#include "Game/Combat/PlayerCombatCommandRuntime.h"

#include <cmath>
#include <optional>
#include <vector>

namespace DeepRun::Tests
{
namespace M5PlayerCombatCommandDetail
{
[[nodiscard]] inline Perception::Track MakeTrack(
    const std::uint64_t trackId,
    const float confidence = 0.90F)
{
    return Perception::Track{
        .trackId = trackId,
        .contactId = trackId + 100U,
        .lifecycle = Perception::TrackLifecycleState::Confirmed,
        .estimatedPositionMeters = Physics::PhysicsVector3{
            .x = 1500.0F + static_cast<float>(trackId), .y = -20.0F, .z = 0.0F},
        .positionUncertaintyMeters = 40.0F,
        .estimatedVelocityMetersPerSecond = std::nullopt,
        .estimatedBearingRadians = 0.20F,
        .bearingUncertaintyRadians = 0.04F,
        .confidence = confidence,
        .observationCount = 3U,
        .firstObservationTimeSeconds = 1.0,
        .lastObservationTimeSeconds = 9.0};
}
}

[[nodiscard]] inline bool RunM5PlayerCombatCommandChecks()
{
    using namespace Game::Combat;
    using namespace M5PlayerCombatCommandDetail;

    const Weapons::WeaponDefinition definition{
        .id = "m5.j1-player-heavyweight",
        .preparationSeconds = 2.0,
        .targeting = Weapons::WeaponTargetingRequirements{
            .minimumTrackConfidence = 0.70F,
            .maximumBearingUncertaintyRadians = 0.10F,
            .maximumPositionUncertaintyMeters = 150.0F,
            .requiresEstimatedPosition = true,
            .allowCoastingTrack = false}};

    auto runtimeResult = PlayerCombatCommandRuntime::Create(definition, 10.0);
    if (!runtimeResult)
    {
        return false;
    }
    auto runtime = std::move(*runtimeResult);

    auto bearingOnly = MakeTrack(10U);
    bearingOnly.estimatedPositionMeters = std::nullopt;
    bearingOnly.positionUncertaintyMeters = std::nullopt;
    auto good = MakeTrack(20U);
    auto weak = MakeTrack(30U, 0.40F);
    auto lost = MakeTrack(5U);
    lost.lifecycle = Perception::TrackLifecycleState::Lost;
    std::vector<Perception::Track> tracks{weak, good, lost, bearingOnly};

    const auto initial = runtime.BuildPresentationSnapshot(tracks);
    if (initial.weaponPhase != Weapons::WeaponPhase::Stored || !initial.canPrepareWeapon ||
        initial.canFireWeapon || initial.selectedTrackId.has_value() || initial.selectedTrackPresent ||
        initial.weaponTargetTrackId.has_value())
    {
        return false;
    }

    // Track selection is commander inspection, not fire-control qualification. It deterministically selects the
    // lowest non-Lost perceived track, even when that track is bearing-only and therefore not fire-qualified.
    const auto selectedBearingOnly = runtime.Execute(
        {.type = PlayerCombatCommandType::SelectNextTrack}, tracks, 10.0);
    if (!selectedBearingOnly || !selectedBearingOnly->accepted || selectedBearingOnly->trackId != 10U ||
        runtime.SelectedTrackId() != 10U)
    {
        return false;
    }
    const auto bearingSnapshot = runtime.BuildPresentationSnapshot(tracks);
    if (!bearingSnapshot.selectedTrackPresent || bearingSnapshot.selectedTrackWeaponQualified ||
        bearingSnapshot.selectedTrackHasEstimatedPosition || bearingSnapshot.canFireWeapon ||
        !bearingSnapshot.selectedTrackConfidence.has_value())
    {
        return false;
    }

    const auto prepared = runtime.Execute({.type = PlayerCombatCommandType::PrepareWeapon}, tracks, 10.0);
    if (!prepared || !prepared->accepted || runtime.Weapon().phase != Weapons::WeaponPhase::Preparing)
    {
        return false;
    }
    const auto earlyFire = runtime.Execute({.type = PlayerCombatCommandType::FireWeapon}, tracks, 10.5);
    if (!earlyFire || earlyFire->accepted || runtime.Weapon().phase != Weapons::WeaponPhase::Preparing ||
        runtime.Weapon().targetTrackId.has_value())
    {
        return false;
    }

    if (!runtime.Advance(12.0) || runtime.Weapon().phase != Weapons::WeaponPhase::Ready)
    {
        return false;
    }
    const auto bearingOnlyFire = runtime.Execute({.type = PlayerCombatCommandType::FireWeapon}, tracks, 12.0);
    if (!bearingOnlyFire || bearingOnlyFire->accepted || runtime.Weapon().phase != Weapons::WeaponPhase::Ready ||
        runtime.Weapon().targetTrackId.has_value())
    {
        return false;
    }

    // Cycling now selects the qualified spatial track. The presentation projection reports readiness/quality
    // without carrying a hostile Transform or physics handle.
    const auto selectedGood = runtime.Execute(
        {.type = PlayerCombatCommandType::SelectNextTrack}, tracks, 12.0);
    if (!selectedGood || !selectedGood->accepted || selectedGood->trackId != 20U)
    {
        return false;
    }
    const auto readySnapshot = runtime.BuildPresentationSnapshot(tracks);
    if (!readySnapshot.selectedTrackWeaponQualified || !readySnapshot.canFireWeapon ||
        readySnapshot.weaponPhase != Weapons::WeaponPhase::Ready ||
        readySnapshot.selectedPositionUncertaintyMeters != good.positionUncertaintyMeters)
    {
        return false;
    }

    // Fire must revalidate the exact current Track. Degrading the selected track on the command tick must reject
    // launch and must not leave a partially assigned target in authoritative weapon state.
    std::vector<Perception::Track> degraded = tracks;
    for (auto& track : degraded)
    {
        if (track.trackId == 20U)
        {
            track.confidence = 0.20F;
        }
    }
    const auto degradedFire = runtime.Execute({.type = PlayerCombatCommandType::FireWeapon}, degraded, 12.25);
    if (!degradedFire || degradedFire->accepted || runtime.Weapon().phase != Weapons::WeaponPhase::Ready ||
        runtime.Weapon().targetTrackId.has_value())
    {
        return false;
    }
    const auto degradedSnapshot = runtime.BuildPresentationSnapshot(degraded);
    if (degradedSnapshot.selectedTrackWeaponQualified || degradedSnapshot.canFireWeapon ||
        !degradedSnapshot.lastCommand.has_value() || degradedSnapshot.lastCommand->accepted)
    {
        return false;
    }

    const auto fired = runtime.Execute({.type = PlayerCombatCommandType::FireWeapon}, tracks, 12.5);
    if (!fired || !fired->accepted || fired->trackId != 20U ||
        runtime.Weapon().phase != Weapons::WeaponPhase::Launched || runtime.Weapon().targetTrackId != 20U)
    {
        return false;
    }
    const auto launchedSnapshot = runtime.BuildPresentationSnapshot(tracks);
    if (launchedSnapshot.canPrepareWeapon || launchedSnapshot.canFireWeapon ||
        launchedSnapshot.weaponTargetTrackId != 20U || !launchedSnapshot.lastCommand ||
        !launchedSnapshot.lastCommand->accepted ||
        launchedSnapshot.lastCommand->command != PlayerCombatCommandType::FireWeapon)
    {
        return false;
    }

    // SimulationTime reversal is an API/invariant error rather than a normal gameplay rejection.
    if (runtime.Execute({.type = PlayerCombatCommandType::SelectNextTrack}, tracks, 12.0) ||
        runtime.Advance(12.0))
    {
        return false;
    }

    // A second runtime proves deterministic cycle/wrap order and that Lost contacts never become commander
    // selection merely because their old track IDs are still present in a supplied perceived-track snapshot.
    auto cycleResult = PlayerCombatCommandRuntime::Create(definition, 20.0);
    if (!cycleResult)
    {
        return false;
    }
    auto cycle = std::move(*cycleResult);
    const auto first = cycle.Execute({.type = PlayerCombatCommandType::SelectNextTrack}, tracks, 20.0);
    const auto second = cycle.Execute({.type = PlayerCombatCommandType::SelectNextTrack}, tracks, 20.0);
    const auto third = cycle.Execute({.type = PlayerCombatCommandType::SelectNextTrack}, tracks, 20.0);
    const auto wrapped = cycle.Execute({.type = PlayerCombatCommandType::SelectNextTrack}, tracks, 20.0);
    if (!first || !second || !third || !wrapped || first->trackId != 10U || second->trackId != 20U ||
        third->trackId != 30U || wrapped->trackId != 10U)
    {
        return false;
    }

    const std::vector<Perception::Track> noSelectable{lost};
    const auto rejectedSelection = cycle.Execute(
        {.type = PlayerCombatCommandType::SelectNextTrack}, noSelectable, 20.0);
    if (!rejectedSelection || rejectedSelection->accepted || cycle.SelectedTrackId() != 10U)
    {
        return false;
    }

    return true;
}
} // namespace DeepRun::Tests
