#pragma once

#include "Game/Combat/PeriscopeCombatRuntime.h"
#include "Game/Combat/PeriscopeObservationSystem.h"
#include "Game/Combat/PlayerCombatCommandRuntime.h"
#include "Game/Submarine/VariableBallastDepthControl.h"
#include "Simulation/Perception/TrackManager.h"

#include <cmath>
#include <string>
#include <vector>

namespace DeepRun::Tests
{
[[nodiscard]] inline bool RunPeriscopeBallastGameplayChecks()
{
    using namespace Game::Combat;
    using namespace Game::Submarine;

    constexpr float WeightNewtons = 100'000'000.0F;
    const VariableBallastDepthControlConfig ballast{};
    const auto surface = CalculateVariableBallastDepthControl(ballast, -1.0F, 0.0F, 0.0F, WeightNewtons);
    const auto dive = CalculateVariableBallastDepthControl(ballast, 1.0F, 0.0F, 0.0F, WeightNewtons);
    const auto neutralArrest = CalculateVariableBallastDepthControl(ballast, 0.0F, 0.0F, 0.5F, WeightNewtons);
    const auto hydrodynamicSpeed = CalculateVariableBallastDepthControl(
        ballast, -1.0F, ballast.zeroAuthorityAboveForwardSpeedMetersPerSecond, 0.0F, WeightNewtons);
    if (!surface || !dive || !neutralArrest || !hydrodynamicSpeed ||
        surface->lowSpeedAuthorityFraction != 1.0F || surface->targetVerticalSpeedMetersPerSecond <= 0.0F ||
        surface->forceNewtons.y <= 0.0F || dive->forceNewtons.y >= 0.0F ||
        neutralArrest->forceNewtons.y >= 0.0F || hydrodynamicSpeed->lowSpeedAuthorityFraction != 0.0F ||
        hydrodynamicSpeed->forceNewtons != Physics::PhysicsVector3{})
    {
        return false;
    }

    auto tracksResult = Perception::TrackManager::Create(Perception::TrackManagerConfig{
        .observationsToConfirm = 1U,
        .maximumTracks = 4U});
    if (!tracksResult)
    {
        return false;
    }
    auto tracks = std::move(*tracksResult);

    const Perception::SensorObservation acoustic{
        .modality = Perception::SensorModality::ActiveAcoustic,
        .sensorId = "TEST_ACTIVE",
        .sensorPositionMeters = Physics::PhysicsVector3{.x = 0.0F, .y = -10.0F, .z = 0.0F},
        .observationTimeSeconds = 0.0,
        .measuredBearingRadians = 0.01F,
        .bearingUncertaintyRadians = 0.04F,
        .estimatedRangeMeters = 1000.0F,
        .rangeUncertaintyMeters = 25.0F,
        .confidence = 0.90F,
        .classificationEvidence = std::nullopt};
    const auto acousticTrackId = tracks.IntegrateObservation(acoustic);
    if (!acousticTrackId)
    {
        return false;
    }
    const auto acousticTracks = tracks.Tracks();
    if (acousticTracks.size() != 1U || acousticTracks.front().visuallyIdentified ||
        acousticTracks.front().classification != Perception::ContactClassification::Unknown ||
        acousticTracks.front().opticalIdentificationLevel != Perception::OpticalIdentificationLevel::None)
    {
        return false;
    }

    auto illegalAcousticClassification = acoustic;
    illegalAcousticClassification.observationTimeSeconds = 0.5;
    illegalAcousticClassification.classificationEvidence = Perception::ContactClassification::CivilianSurfaceVessel;
    if (tracks.IntegrateObservation(illegalAcousticClassification))
    {
        return false;
    }
    auto illegalAcousticDetail = acoustic;
    illegalAcousticDetail.observationTimeSeconds = 0.5;
    illegalAcousticDetail.opticalIdentificationLevel = Perception::OpticalIdentificationLevel::Detected;
    if (tracks.IntegrateObservation(illegalAcousticDetail))
    {
        return false;
    }

    const PeriscopeObservationConfig optics{};
    const PeriscopeState raisedPeriscope{.raised = true, .viewBearingRadians = 0.0F};
    const Physics::PhysicsVector3 periscopeOwnship{.x = 0.0F, .y = -10.0F, .z = 0.0F};
    const auto farSilhouette = ObserveThroughPeriscope(
        optics,
        raisedPeriscope,
        periscopeOwnship,
        10.0F,
        0.0F,
        PeriscopeTargetTruth{
            .positionMeters = {.x = 15'000.0F, .y = 0.0F, .z = 0.0F},
            .visualClassification = Perception::ContactClassification::MilitarySurfaceCombatant,
            .visibleHeightAboveSurfaceMeters = 20.0F},
        0.6);
    if (!farSilhouette || !farSilhouette->has_value() ||
        (*farSilhouette)->opticalIdentificationLevel != Perception::OpticalIdentificationLevel::Detected ||
        (*farSilhouette)->classificationEvidence.has_value() || (*farSilhouette)->estimatedRangeMeters.has_value())
    {
        return false;
    }

    const auto typeResolved = ObserveThroughPeriscope(
        optics,
        raisedPeriscope,
        periscopeOwnship,
        10.0F,
        0.0F,
        PeriscopeTargetTruth{
            .positionMeters = {.x = 8'000.0F, .y = 0.0F, .z = 0.0F},
            .visualClassification = Perception::ContactClassification::MilitarySurfaceCombatant,
            .visibleHeightAboveSurfaceMeters = 20.0F},
        0.7);
    if (!typeResolved || !typeResolved->has_value() ||
        (*typeResolved)->opticalIdentificationLevel != Perception::OpticalIdentificationLevel::TypeResolved ||
        (*typeResolved)->classificationEvidence != Perception::ContactClassification::MilitarySurfaceCombatant ||
        !(*typeResolved)->estimatedRangeMeters.has_value())
    {
        return false;
    }

    const auto flagResolved = ObserveThroughPeriscope(
        optics,
        raisedPeriscope,
        periscopeOwnship,
        10.0F,
        0.0F,
        PeriscopeTargetTruth{
            .positionMeters = {.x = 3'000.0F, .y = 0.0F, .z = 0.0F},
            .visualClassification = Perception::ContactClassification::CivilianSurfaceVessel,
            .visibleHeightAboveSurfaceMeters = 18.0F},
        0.8);
    if (!flagResolved || !flagResolved->has_value() ||
        (*flagResolved)->opticalIdentificationLevel != Perception::OpticalIdentificationLevel::FlagOrMarkingsResolved ||
        (*flagResolved)->classificationEvidence != Perception::ContactClassification::CivilianSurfaceVessel)
    {
        return false;
    }

    const auto poorVisibility = ObserveThroughPeriscope(
        optics,
        raisedPeriscope,
        periscopeOwnship,
        10.0F,
        0.0F,
        PeriscopeTargetTruth{
            .positionMeters = {.x = 8'000.0F, .y = 0.0F, .z = 0.0F},
            .visualClassification = Perception::ContactClassification::MilitarySurfaceCombatant},
        0.9,
        PeriscopeOpticalConditions{.meteorologicalVisibilityMeters = 5'000.0F});
    if (!poorVisibility || poorVisibility->has_value())
    {
        return false;
    }

    const auto lowLight = ObserveThroughPeriscope(
        optics,
        raisedPeriscope,
        periscopeOwnship,
        10.0F,
        0.0F,
        PeriscopeTargetTruth{
            .positionMeters = {.x = 8'000.0F, .y = 0.0F, .z = 0.0F},
            .visualClassification = Perception::ContactClassification::MilitarySurfaceCombatant},
        0.95,
        PeriscopeOpticalConditions{
            .meteorologicalVisibilityMeters = 20'000.0F,
            .ambientLightFraction = 0.25F,
            .glareFraction = 0.0F,
            .seaStateObscurationFraction = 0.0F});
    if (!lowLight || !lowLight->has_value() ||
        (*lowLight)->opticalIdentificationLevel != Perception::OpticalIdentificationLevel::Detected ||
        (*lowLight)->classificationEvidence.has_value())
    {
        return false;
    }

    const auto optical = ObserveThroughPeriscope(
        optics,
        raisedPeriscope,
        periscopeOwnship,
        10.0F,
        0.0F,
        PeriscopeTargetTruth{
            .positionMeters = {.x = 1000.0F, .y = 0.0F, .z = 0.0F},
            .visualClassification = Perception::ContactClassification::CivilianSurfaceVessel},
        1.0);
    if (!optical || !optical->has_value() ||
        (*optical)->modality != Perception::SensorModality::Optical ||
        (*optical)->classificationEvidence != Perception::ContactClassification::CivilianSurfaceVessel ||
        (*optical)->opticalIdentificationLevel != Perception::OpticalIdentificationLevel::FlagOrMarkingsResolved)
    {
        return false;
    }
    const auto identifiedTrackId = tracks.IntegrateObservation(**optical);
    if (!identifiedTrackId || *identifiedTrackId != *acousticTrackId)
    {
        return false;
    }
    const auto identifiedTracks = tracks.Tracks();
    if (identifiedTracks.size() != 1U || !identifiedTracks.front().visuallyIdentified ||
        identifiedTracks.front().classification != Perception::ContactClassification::CivilianSurfaceVessel ||
        identifiedTracks.front().opticalIdentificationLevel != Perception::OpticalIdentificationLevel::FlagOrMarkingsResolved)
    {
        return false;
    }

    const auto tooDeep = ObserveThroughPeriscope(
        optics,
        raisedPeriscope,
        {.x = 0.0F, .y = -30.0F, .z = 0.0F},
        30.0F,
        0.0F,
        PeriscopeTargetTruth{
            .positionMeters = {.x = 1000.0F, .y = 0.0F, .z = 0.0F},
            .visualClassification = Perception::ContactClassification::MilitarySurfaceCombatant},
        2.0);
    if (!tooDeep || tooDeep->has_value())
    {
        return false;
    }

    // Live command bridge: raise from a perceived Track, run optical simulation, fuse evidence into that exact
    // Track, and project only the resulting periscope/Track state into UI-facing presentation.
    auto liveTracksResult = Perception::TrackManager::Create(Perception::TrackManagerConfig{
        .observationsToConfirm = 1U,
        .maximumTracks = 4U});
    if (!liveTracksResult)
    {
        return false;
    }
    auto liveTracks = std::move(*liveTracksResult);
    const auto liveTrackId = liveTracks.IntegrateObservation(acoustic);
    if (!liveTrackId)
    {
        return false;
    }
    PeriscopeState livePeriscope{};
    const auto raised = TogglePeriscopeForSelectedTrack(livePeriscope, liveTracks, *liveTrackId, 10.0F);
    if (!raised.accepted || !livePeriscope.raised)
    {
        return false;
    }
    const auto liveVisual = VisualIdentifySelectedTrack(
        livePeriscope,
        liveTracks,
        *liveTrackId,
        {.x = 0.0F, .y = -10.0F, .z = 0.0F},
        10.0F,
        0.0F,
        PeriscopeTargetTruth{
            .positionMeters = {.x = 1000.0F, .y = 0.0F, .z = 0.0F},
            .visualClassification = Perception::ContactClassification::MilitarySurfaceCombatant},
        1.0);
    if (!liveVisual || !liveVisual->accepted)
    {
        return false;
    }
    const auto liveSnapshotTracks = liveTracks.Tracks();
    if (liveSnapshotTracks.size() != 1U || liveSnapshotTracks.front().trackId != *liveTrackId ||
        !liveSnapshotTracks.front().visuallyIdentified ||
        liveSnapshotTracks.front().classification != Perception::ContactClassification::MilitarySurfaceCombatant)
    {
        return false;
    }
    PlayerCombatPresentationSnapshot livePresentation{
        .selectedTrackId = *liveTrackId,
        .selectedTrackPresent = true,
        .selectedTrackLifecycle = liveSnapshotTracks.front().lifecycle};
    ApplyPeriscopePresentation(livePresentation, livePeriscope, 10.0F);
    if (!livePresentation.periscopeWithinOperatingDepth || !livePresentation.periscopeRaised ||
        !livePresentation.periscopeMastExposed || !livePresentation.canVisualIdentify)
    {
        return false;
    }
    const auto stowed = TogglePeriscopeForSelectedTrack(livePeriscope, liveTracks, *liveTrackId, 10.0F);
    if (!stowed.accepted || livePeriscope.raised)
    {
        return false;
    }
    PeriscopeState deepPeriscope{};
    const auto rejectedDeepRaise = TogglePeriscopeForSelectedTrack(deepPeriscope, liveTracks, *liveTrackId, 30.0F);
    if (rejectedDeepRaise.accepted || deepPeriscope.raised)
    {
        return false;
    }

    const Weapons::WeaponDefinition weapon{
        .id = "periscope-roe-test",
        .preparationSeconds = 0.0,
        .targeting = Weapons::WeaponTargetingRequirements{
            .minimumTrackConfidence = 0.65F,
            .maximumBearingUncertaintyRadians = 0.10F,
            .maximumPositionUncertaintyMeters = 150.0F,
            .requiresEstimatedPosition = true,
            .allowCoastingTrack = false}};

    auto civilianRuntimeResult = PlayerCombatCommandRuntime::Create(weapon, 2.0);
    if (!civilianRuntimeResult)
    {
        return false;
    }
    auto civilianRuntime = std::move(*civilianRuntimeResult);
    const std::vector<Perception::Track> civilianTracks{identifiedTracks.front()};
    if (!civilianRuntime.Execute({.type = PlayerCombatCommandType::SelectNextTrack}, civilianTracks, 2.0) ||
        !civilianRuntime.Execute({.type = PlayerCombatCommandType::PrepareWeapon}, civilianTracks, 2.0) ||
        !civilianRuntime.Advance(2.0))
    {
        return false;
    }
    const auto civilianSnapshot = civilianRuntime.BuildPresentationSnapshot(civilianTracks);
    if (!civilianSnapshot.selectedTrackWeaponQualified || civilianSnapshot.selectedTrackRulesOfEngagementQualified ||
        !civilianSnapshot.selectedTrackVisuallyIdentified || civilianSnapshot.canFireWeapon)
    {
        return false;
    }
    const auto civilianFire = civilianRuntime.Execute(
        {.type = PlayerCombatCommandType::FireWeapon}, civilianTracks, 2.0);
    if (!civilianFire || civilianFire->accepted ||
        civilianFire->message.find("CIVILIAN") == std::string::npos)
    {
        return false;
    }

    auto unknownTrack = identifiedTracks.front();
    unknownTrack.classification = Perception::ContactClassification::Unknown;
    unknownTrack.visuallyIdentified = false;
    unknownTrack.opticalIdentificationLevel = Perception::OpticalIdentificationLevel::Detected;
    const std::vector<Perception::Track> unknownTracks{unknownTrack};
    auto riskRuntimeResult = PlayerCombatCommandRuntime::Create(weapon, 3.0);
    if (!riskRuntimeResult)
    {
        return false;
    }
    auto riskRuntime = std::move(*riskRuntimeResult);
    if (!riskRuntime.Execute({.type = PlayerCombatCommandType::SelectNextTrack}, unknownTracks, 3.0) ||
        !riskRuntime.Execute({.type = PlayerCombatCommandType::PrepareWeapon}, unknownTracks, 3.0) ||
        !riskRuntime.Advance(3.0))
    {
        return false;
    }
    const auto riskSnapshot = riskRuntime.BuildPresentationSnapshot(unknownTracks);
    if (!riskSnapshot.selectedTrackWeaponQualified || !riskSnapshot.selectedTrackRulesOfEngagementQualified ||
        !riskSnapshot.selectedTrackCivilianRisk || !riskSnapshot.canFireWeapon)
    {
        return false;
    }
    const auto riskFire = riskRuntime.Execute({.type = PlayerCombatCommandType::FireWeapon}, unknownTracks, 3.0);
    if (!riskFire || !riskFire->accepted || riskFire->message.find("civilian-risk") == std::string::npos)
    {
        return false;
    }

    return true;
}
} // namespace DeepRun::Tests
