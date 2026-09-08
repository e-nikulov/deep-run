#pragma once

#include "Simulation/Acoustics/AcousticWorld.h"
#include "Simulation/Perception/SensorObservation.h"
#include "Simulation/Perception/TrackManager.h"
#include "Simulation/Weapons/AcousticDecoy.h"
#include "Simulation/Weapons/TorpedoSeeker.h"

#include <cmath>
#include <optional>

namespace DeepRun::Tests
{
namespace M5AcousticDecoyDetail
{
[[nodiscard]] inline Acoustics::AcousticSpectrum UniformSpectrum(const float level) noexcept
{
    return Acoustics::AcousticSpectrum{.levelDb = {level, level, level, level}};
}

[[nodiscard]] inline Perception::Track MakeLaunchTrack()
{
    return Perception::Track{
        .trackId = 900U,
        .contactId = 90U,
        .lifecycle = Perception::TrackLifecycleState::Confirmed,
        .estimatedPositionMeters = Physics::PhysicsVector3{.x = 1200.0F, .y = 0.0F, .z = 0.0F},
        .positionUncertaintyMeters = 30.0F,
        .estimatedVelocityMetersPerSecond = std::nullopt,
        .estimatedBearingRadians = 0.0F,
        .bearingUncertaintyRadians = 0.04F,
        .confidence = 0.90F,
        .observationCount = 4U,
        .firstObservationTimeSeconds = 0.0,
        .lastObservationTimeSeconds = 0.0};
}
}

[[nodiscard]] inline bool RunM5AcousticDecoyChecks()
{
    using namespace M5AcousticDecoyDetail;
    using namespace Weapons;

    const AcousticDecoyDefinition decoyDefinition{
        .id = "m5.acoustic-decoy",
        .continuousSourceLevelDb = UniformSpectrum(155.0F),
        .driftVelocityMetersPerSecond = {.x = 1.0F, .y = -0.5F, .z = 0.0F},
        .activeLifetimeSeconds = 5.0};
    auto decoyResult = DeployAcousticDecoy(
        decoyDefinition,
        {.x = 600.0F, .y = 400.0F, .z = 0.0F},
        0.0);
    if (!decoyResult)
    {
        return false;
    }
    auto decoy = *decoyResult;

    // A decoy emission is a snapshot of the current runtime moment. Back-dating from a newer state is forbidden.
    const auto invalidFutureTimestamp = SampleAcousticDecoyEmission(decoyDefinition, decoy, 1.0);
    if (invalidFutureTimestamp)
    {
        return false;
    }
    const auto decoyEmissionResult = SampleAcousticDecoyEmission(decoyDefinition, decoy, 0.0);
    if (!decoyEmissionResult || !*decoyEmissionResult)
    {
        return false;
    }
    const auto decoyEmission = **decoyEmissionResult;

    const auto acousticWorldResult = Acoustics::AcousticWorld::Create({});
    if (!acousticWorldResult)
    {
        return false;
    }
    const auto acousticWorld = *acousticWorldResult;
    const Acoustics::AcousticReceiver seekerReceiver{
        .sensorId = "M5_TORPEDO_PASSIVE_SEEKER",
        .positionMeters = {.x = 0.0F, .y = 0.0F, .z = 0.0F},
        .ambientNoiseLevelDb = UniformSpectrum(40.0F),
        .selfNoiseLevelDb = UniformSpectrum(40.0F),
        .sensitivityDb = UniformSpectrum(0.0F),
        .minimumPeakSnrDb = 3.0F};

    // Real source and decoy enter exactly the same acoustic pipeline. Neither observation carries source
    // identity. The decoy is merely stronger/closer in this authored test scenario.
    const Acoustics::AcousticEmission realSourceEmission{
        .positionMeters = {.x = 1200.0F, .y = 0.0F, .z = 0.0F},
        .sourceLevelDb = UniformSpectrum(125.0F),
        .emissionTimeSeconds = 0.0};
    const auto realObserved = acousticWorld.CollectPassiveDirectObservation(
        realSourceEmission,
        seekerReceiver,
        1.0);
    const auto decoyObserved = acousticWorld.CollectPassiveDirectObservation(
        decoyEmission,
        seekerReceiver,
        1.0);
    if (!realObserved || !*realObserved || !decoyObserved || !*decoyObserved)
    {
        return false;
    }
    if ((*realObserved)->estimatedRangeMeters.has_value() || (*decoyObserved)->estimatedRangeMeters.has_value())
    {
        return false;
    }

    const auto realPerceived = Perception::FromAcousticObservation(**realObserved);
    const auto decoyPerceived = Perception::FromAcousticObservation(**decoyObserved);
    if (!realPerceived || !decoyPerceived)
    {
        return false;
    }

    auto trackManagerResult = Perception::TrackManager::Create(Perception::TrackManagerConfig{
        .associationGateRadians = 0.20F,
        .observationsToConfirm = 1U,
        .coastAfterSeconds = 5.0,
        .lostAfterSeconds = 20.0,
        .confidenceDecayPerSecond = 0.0F,
        .bearingUncertaintyGrowthRadiansPerSecond = 0.0F,
        .positionUncertaintyGrowthMetersPerSecond = 0.0F,
        .maximumTracks = 8U});
    if (!trackManagerResult)
    {
        return false;
    }
    auto trackManager = *trackManagerResult;
    const auto realTrackId = trackManager.IntegrateObservation(*realPerceived);
    const auto decoyTrackId = trackManager.IntegrateObservation(*decoyPerceived);
    if (!realTrackId || !decoyTrackId || *realTrackId == *decoyTrackId)
    {
        return false;
    }

    const auto tracks = trackManager.Tracks();
    if (tracks.size() != 2U)
    {
        return false;
    }

    TorpedoSeekerRuntimeState seekerState{};
    const TorpedoSeekerConfig seekerConfig{
        .minimumTrackConfidence = 0.35F,
        .maximumBearingUncertaintyRadians = 0.20F,
        .allowCoastingTrack = false};
    const auto cueResult = SelectTorpedoSeekerCue(seekerConfig, seekerState, tracks, 1.0);
    if (!cueResult || !*cueResult || (*cueResult)->seekerTrackId != *decoyTrackId ||
        seekerState.selectedTrackId != *decoyTrackId || (*cueResult)->bearingRadians <= 0.4F)
    {
        return false;
    }
    const auto selectedBeforeTimeReverse = seekerState.selectedTrackId;
    if (SelectTorpedoSeekerCue(seekerConfig, seekerState, tracks, 0.5) ||
        seekerState.selectedTrackId != selectedBeforeTimeReverse || seekerState.lastUpdateTimeSeconds != 1.0)
    {
        return false;
    }

    // Launch/datalink track remains a separate perceived identity. Local seeker acquisition may choose another
    // track (the decoy) without obtaining any source entity/body/Transform identity.
    const WeaponDefinition weaponDefinition{
        .id = "m5.decoy-seeker-heavyweight",
        .preparationSeconds = 0.0,
        .targeting = WeaponTargetingRequirements{
            .minimumTrackConfidence = 0.70F,
            .maximumBearingUncertaintyRadians = 0.10F,
            .maximumPositionUncertaintyMeters = 150.0F,
            .requiresEstimatedPosition = true,
            .allowCoastingTrack = false}};
    const ConventionalTorpedoDefinition torpedoDefinition{
        .weapon = weaponDefinition,
        .underwaterSpeedMetersPerSecond = 20.0F,
        .maximumTurnRateRadiansPerSecond = 0.25F};
    const auto launchTrack = MakeLaunchTrack();
    auto weaponRuntimeResult = CreateWeaponRuntime(weaponDefinition, 0.0);
    if (!weaponRuntimeResult)
    {
        return false;
    }
    auto weaponRuntime = *weaponRuntimeResult;
    if (!PrepareWeapon(weaponDefinition, weaponRuntime, 0.0) ||
        !AssignWeaponTarget(weaponDefinition, weaponRuntime, launchTrack, 0.0) ||
        !LaunchWeapon(weaponDefinition, weaponRuntime, 0.0))
    {
        return false;
    }
    auto torpedoResult = CreateLaunchedConventionalTorpedo(
        torpedoDefinition,
        weaponRuntime,
        {.x = 0.0F, .y = 0.0F, .z = 0.0F},
        0.0F,
        launchTrack,
        0.0);
    if (!torpedoResult)
    {
        return false;
    }
    auto torpedo = *torpedoResult;
    const auto originalLaunchTrackId = torpedo.guidanceTrackId;

    auto weakCue = **cueResult;
    weakCue.confidence = 0.10F;
    if (AdvanceConventionalTorpedoWithSeekerCue(torpedoDefinition, seekerConfig, torpedo, weakCue, 1.0) ||
        torpedo.lastUpdateTimeSeconds != 0.0 || torpedo.headingRadians != 0.0F)
    {
        return false;
    }

    if (!AdvanceConventionalTorpedoWithSeekerCue(torpedoDefinition, seekerConfig, torpedo, **cueResult, 1.0) ||
        !originalLaunchTrackId || torpedo.guidanceTrackId != originalLaunchTrackId ||
        seekerState.selectedTrackId == originalLaunchTrackId ||
        std::abs(torpedo.headingRadians - 0.25F) > 0.001F || torpedo.positionMeters.y <= 0.0F)
    {
        return false;
    }

    if (!AdvanceAcousticDecoy(decoyDefinition, decoy, 2.0) ||
        std::abs(decoy.emitter.positionMeters.x - 602.0F) > 0.001F ||
        std::abs(decoy.emitter.positionMeters.y - 399.0F) > 0.001F || !decoy.active)
    {
        return false;
    }
    if (SampleAcousticDecoyEmission(decoyDefinition, decoy, 1.0))
    {
        return false;
    }
    if (AdvanceAcousticDecoy(decoyDefinition, decoy, 1.5))
    {
        return false;
    }
    if (!AdvanceAcousticDecoy(decoyDefinition, decoy, 5.0) || decoy.active ||
        std::abs(decoy.emitter.positionMeters.x - 605.0F) > 0.001F ||
        std::abs(decoy.emitter.positionMeters.y - 397.5F) > 0.001F)
    {
        return false;
    }
    const auto expiredEmission = SampleAcousticDecoyEmission(decoyDefinition, decoy, 5.0);
    if (!expiredEmission || expiredEmission->has_value())
    {
        return false;
    }

    return true;
}
} // namespace DeepRun::Tests
