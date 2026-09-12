#pragma once

#include "Simulation/Acoustics/AcousticWorld.h"
#include "Simulation/Acoustics/ActiveSonar.h"
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

    // Mixed-mode state machine: launch-clearance is dormant, then passive search; absent passive contact causes
    // active search, an active echo can own guidance, good passive evidence is preferred when it returns, and
    // contact loss enters reacquisition. These are deliberately authored GAME POLICY timings.
    const TorpedoSeekerModeConfig modeConfig{
        .passiveSearchBeforeActiveSeconds = 0.5,
        .activePingIntervalSeconds = 0.5,
        .lostContactBeforeActiveSeconds = 0.25,
        .maximumSearchWithoutContactSeconds = 2.0,
        .preferPassiveCue = true};
    TorpedoSeekerRuntimeState modeState{};
    const auto dormant = UpdateTorpedoSeekerMode(
        seekerConfig, modeConfig, modeState, false, std::nullopt, std::nullopt, 0.0);
    const auto passiveSearch = UpdateTorpedoSeekerMode(
        seekerConfig, modeConfig, modeState, true, std::nullopt, std::nullopt, 0.0);
    const auto activeSearch = UpdateTorpedoSeekerMode(
        seekerConfig, modeConfig, modeState, true, std::nullopt, std::nullopt, 0.6);
    if (!dormant || dormant->mode != TorpedoSeekerMode::Dormant || dormant->requestActivePing ||
        !passiveSearch || passiveSearch->mode != TorpedoSeekerMode::PassiveSearch || passiveSearch->requestActivePing ||
        !activeSearch || activeSearch->mode != TorpedoSeekerMode::ActiveSearch || !activeSearch->requestActivePing ||
        !NotifyTorpedoSeekerActivePingEmitted(modeConfig, modeState, 0.6) || modeState.activePingCount != 1U)
    {
        return false;
    }

    const TorpedoSeekerCue activeCue{
        .seekerTrackId = 777U,
        .bearingRadians = 0.10F,
        .bearingUncertaintyRadians = 0.05F,
        .confidence = 0.80F};
    const auto activeTrack = UpdateTorpedoSeekerMode(
        seekerConfig, modeConfig, modeState, true, std::nullopt, activeCue, 0.7);
    if (!activeTrack || activeTrack->mode != TorpedoSeekerMode::ActiveTrack ||
        !activeTrack->guidanceCue || activeTrack->guidanceCue->seekerTrackId != activeCue.seekerTrackId)
    {
        return false;
    }
    const auto passiveTrack = UpdateTorpedoSeekerMode(
        seekerConfig, modeConfig, modeState, true, **cueResult, activeCue, 0.8);
    if (!passiveTrack || passiveTrack->mode != TorpedoSeekerMode::PassiveTrack ||
        !passiveTrack->guidanceCue || passiveTrack->guidanceCue->seekerTrackId != (*cueResult)->seekerTrackId)
    {
        return false;
    }
    const auto reacquireEarly = UpdateTorpedoSeekerMode(
        seekerConfig, modeConfig, modeState, true, std::nullopt, std::nullopt, 1.0);
    const auto reacquireActive = UpdateTorpedoSeekerMode(
        seekerConfig, modeConfig, modeState, true, std::nullopt, std::nullopt, 1.2);
    if (!reacquireEarly || reacquireEarly->mode != TorpedoSeekerMode::Reacquire || reacquireEarly->requestActivePing ||
        !reacquireActive || reacquireActive->mode != TorpedoSeekerMode::Reacquire || !reacquireActive->requestActivePing ||
        modeState.missReason != TorpedoSeekerMissReason::ContactLost ||
        UpdateTorpedoSeekerMode(seekerConfig, modeConfig, modeState, true, std::nullopt, std::nullopt, 1.1))
    {
        return false;
    }

    const TorpedoSeekerModeConfig shortSearchConfig{
        .passiveSearchBeforeActiveSeconds = 0.2,
        .activePingIntervalSeconds = 0.5,
        .lostContactBeforeActiveSeconds = 0.1,
        .maximumSearchWithoutContactSeconds = 1.0,
        .preferPassiveCue = true};
    TorpedoSeekerRuntimeState exhaustedState{};
    if (!UpdateTorpedoSeekerMode(seekerConfig, shortSearchConfig, exhaustedState, true, std::nullopt, std::nullopt, 0.0))
    {
        return false;
    }
    const auto exhausted = UpdateTorpedoSeekerMode(
        seekerConfig, shortSearchConfig, exhaustedState, true, std::nullopt, std::nullopt, 1.1);
    if (!exhausted || exhausted->mode != TorpedoSeekerMode::Exhausted || exhausted->guidanceCue ||
        exhaustedState.missReason != TorpedoSeekerMissReason::NoInitialAcquisition)
    {
        return false;
    }

    // A quiet source can be inaudible passively while a local active pulse still produces a delayed echo. An
    // off-beam reflector produces no echo. This proves two distinct sensor modes without exposing source identity.
    const Acoustics::AcousticEmission quietSource{
        .positionMeters = {.x = 400.0F, .y = 0.0F, .z = 0.0F},
        .sourceLevelDb = UniformSpectrum(70.0F),
        .emissionTimeSeconds = 0.0};
    const auto quietPassive = acousticWorld.CollectPassiveDirectObservation(quietSource, seekerReceiver, 1.0);
    if (!quietPassive || quietPassive->has_value())
    {
        return false;
    }
    const Acoustics::ActiveAcousticPulse localPulse{
        .originMeters = seekerReceiver.positionMeters,
        .forwardUnitVector = {.x = 1.0F, .y = 0.0F, .z = 0.0F},
        .sourceLevelDb = UniformSpectrum(190.0F),
        .beamHalfAngleRadians = 0.35F,
        .emissionTimeSeconds = 0.0};
    const Acoustics::AcousticReflector inBeamReflector{
        .positionMeters = quietSource.positionMeters,
        .reflectionLossDb = UniformSpectrum(8.0F)};
    const auto tooEarlyEcho = Acoustics::CollectMonostaticActiveEchoObservation(
        acousticWorld, localPulse, inBeamReflector, seekerReceiver, 0.2);
    const auto activeEcho = Acoustics::CollectMonostaticActiveEchoObservation(
        acousticWorld, localPulse, inBeamReflector, seekerReceiver, 0.6);
    const Acoustics::AcousticReflector offBeamReflector{
        .positionMeters = {.x = 0.0F, .y = 400.0F, .z = 0.0F},
        .reflectionLossDb = UniformSpectrum(8.0F)};
    const auto offBeamEcho = Acoustics::CollectMonostaticActiveEchoObservation(
        acousticWorld, localPulse, offBeamReflector, seekerReceiver, 1.0);
    if (!tooEarlyEcho || tooEarlyEcho->has_value() || !activeEcho || !activeEcho->has_value() ||
        !(*activeEcho)->estimatedRangeMeters || !offBeamEcho || offBeamEcho->has_value())
    {
        return false;
    }

    const auto activePerceived = Perception::FromAcousticObservation(**activeEcho, localPulse.originMeters);
    if (!activePerceived)
    {
        return false;
    }
    auto activeTrackManagerResult = Perception::TrackManager::Create(Perception::TrackManagerConfig{
        .associationGateRadians = 0.20F,
        .observationsToConfirm = 1U,
        .coastAfterSeconds = 0.5,
        .lostAfterSeconds = 1.5,
        .confidenceDecayPerSecond = 0.2F,
        .bearingUncertaintyGrowthRadiansPerSecond = 0.02F,
        .positionUncertaintyGrowthMetersPerSecond = 1.0F,
        .maximumTracks = 4U});
    if (!activeTrackManagerResult)
    {
        return false;
    }
    auto activeTrackManager = *activeTrackManagerResult;
    if (!activeTrackManager.IntegrateObservation(*activePerceived))
    {
        return false;
    }
    const auto activeLocalCue = SelectBestTorpedoSeekerCue(seekerConfig, activeTrackManager.Tracks());
    if (!activeLocalCue || !activeLocalCue->has_value() ||
        std::abs((*activeLocalCue)->bearingRadians) > 0.01F)
    {
        return false;
    }

    // Launch/fire-control track remains a separate perceived identity. Local seeker acquisition may choose another
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

    // The 1 radian local bearing demand can only change heading by the authored 0.25 rad/s turn limit in one
    // second. A hard crossing manoeuvre can therefore outrun the seeker/vehicle turn authority and cause a miss.
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
