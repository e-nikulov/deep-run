#pragma once

#include "Game/Combat/PeriscopeCombatRuntime.h"
#include "Game/Combat/PeriscopeObservationSystem.h"
#include "Game/Combat/PlayerCombatCommandRuntime.h"
#include "Game/Submarine/AnteyBallastControl.h"
#include "Game/Submarine/AnteyHandlingModel.h"
#include "Game/Submarine/VariableBallastDepthControl.h"
#include "Game/Weapons/AnteyTorpedoInventory.h"
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
    constexpr float SeaWaterDensity = 1025.0F;
    constexpr float SurfaceWaterplaneHalfHeight = 4.35F;
    const float publicReserve = AnteyMainBallastWaterCapacityKg / AnteyPublicSurfaceDisplacementMassKg;
    const float gameplayReserve = AnteyGameplayMainBallastWaterCapacityKg / AnteyGameplaySurfaceMassKg;
    const float surfacedFraction = AnteyGameplaySurfaceMassKg / AnteyGameplayFullSubmergedMassKg;
    const float surfaceCenterDepth = SurfaceWaterplaneHalfHeight * (2.0F * surfacedFraction - 1.0F);
    const float fullDisplacedVolume = AnteyGameplayFullSubmergedMassKg / SeaWaterDensity;
    const float submergedTerminal = std::sqrt(
        2.0F * AnteyGameplayPropulsion.maxForwardThrustNewtons / (SeaWaterDensity * 24.11986F));
    const float surfacedTerminal = std::sqrt(
        2.0F * AnteyGameplayPropulsion.maxForwardThrustNewtons / (SeaWaterDensity * 109.77216F));
    constexpr float MaximumTrajectoryAngleRadians = 0.436332313F;
    const float maximumHydrodynamicVertical =
        AnteyPublicMaximumSubmergedSpeedMetersPerSecond * std::sin(MaximumTrajectoryAngleRadians);
    const float maximumPositiveBuoyancyNewtons =
        AnteyGameplayMainBallastWaterCapacityKg * 9.81F;
    const float maximumBallastOnlyVertical = std::sqrt(
        2.0F * maximumPositiveBuoyancyNewtons / (SeaWaterDensity * 1800.0F));
    if (std::abs(publicReserve - 0.2991903F) > 1.0e-4F ||
        std::abs(gameplayReserve - 0.2991903F) > 1.0e-4F ||
        std::abs(surfaceCenterDepth - 2.346479F) > 0.001F ||
        std::abs(fullDisplacedVolume - 18'784.390F) > 0.02F ||
        std::abs(submergedTerminal - AnteyPublicMaximumSubmergedSpeedMetersPerSecond) > 0.02F ||
        std::abs(surfacedTerminal - AnteyPublicMaximumSurfacedSpeedMetersPerSecond) > 0.02F ||
        std::abs(maximumHydrodynamicVertical - 6.9572F) > 0.03F ||
        std::abs(maximumBallastOnlyVertical - 6.8667F) > 0.03F)
        return false;
    const AnteyBallastControlConfig ballastStateConfig{};
    const AnteyBallastState emptyMainBallast{.mainBallastFillFraction = 0.0F, .trimMassDeltaKg = 0.0F};
    const AnteyBallastState fullMainBallast{};
    if (std::abs(AnteyPhysicalMassKg(ballastStateConfig, emptyMainBallast) - AnteyGameplaySurfaceMassKg) > 1.0F ||
        std::abs(AnteyPhysicalMassKg(ballastStateConfig, fullMainBallast) - AnteyGameplayFullSubmergedMassKg) > 1.0F)
        return false;
    const auto deepSurfaceBallast = AdvanceAnteyBallastState(
        ballastStateConfig, fullMainBallast, -1.0F, 100.0F, -100'000.0F, 0.0F, 1.0F);
    const auto nearSurfaceBallast = AdvanceAnteyBallastState(
        ballastStateConfig, fullMainBallast, -1.0F, 2.5F, -100'000.0F, 0.0F, 1.0F);
    const AnteyBallastState partlyBlown{.mainBallastFillFraction = 0.5F, .trimMassDeltaKg = 0.0F};
    const auto diveFromSurfaceBallast = AdvanceAnteyBallastState(
        ballastStateConfig, partlyBlown, 1.0F, 2.0F, 100'000.0F, 0.0F, 1.0F);
    if (!deepSurfaceBallast || !nearSurfaceBallast || !diveFromSurfaceBallast ||
        std::abs(deepSurfaceBallast->mainBallastFillFraction - 1.0F) > 1.0e-6F ||
        !(nearSurfaceBallast->mainBallastFillFraction < 1.0F) ||
        !(diveFromSurfaceBallast->mainBallastFillFraction > partlyBlown.mainBallastFillFraction) ||
        !(deepSurfaceBallast->trimMassDeltaKg < 0.0F) ||
        std::abs(deepSurfaceBallast->trimMassDeltaKg) >= 100'000.0F)
        return false;

    // A fired weapon removes mass immediately; dedicated compensation water then restores it at a finite rate.
    const auto oneSecondAfterP700 = AdvanceAnteyBallastState(
        ballastStateConfig, fullMainBallast, 0.0F, 100.0F, 0.0F,
        Game::Armament::P700GranitRoundMassKg, 1.0F);
    if (!oneSecondAfterP700 ||
        std::abs(oneSecondAfterP700->weaponCompensationWaterMassKg - 2'800.0F) > 1.0F ||
        std::abs(AnteyPhysicalMassKg(
            ballastStateConfig, *oneSecondAfterP700, Game::Armament::P700GranitRoundMassKg) -
            (AnteyGameplayFullSubmergedMassKg - 4'200.0F)) > 2.0F)
        return false;
    const AnteyBallastState fullyCompensatedP700{
        .mainBallastFillFraction = 1.0F, .trimMassDeltaKg = 0.0F,
        .weaponCompensationWaterMassKg = Game::Armament::P700GranitRoundMassKg};
    if (std::abs(AnteyPhysicalMassKg(
            ballastStateConfig, fullyCompensatedP700, Game::Armament::P700GranitRoundMassKg) -
            AnteyGameplayFullSubmergedMassKg) > 1.0F)
        return false;

    Game::Armament::AnteyTorpedoInventory torpedoes{};
    if (torpedoes.LoadedCount(Game::Armament::PlayerWeaponType::HeavyweightTorpedo) != 18U ||
        torpedoes.LoadedCount(Game::Armament::PlayerWeaponType::Type6576AFast) != 10U ||
        torpedoes.ExpendedMassKg() != 0.0F ||
        Game::Armament::AnteyConfiguredCombatOrdnanceMassKg != 249'000.0F)
        return false;
    if (!torpedoes.Consume(Game::Armament::PlayerWeaponType::HeavyweightTorpedo) ||
        !torpedoes.Consume(Game::Armament::PlayerWeaponType::Type6576AEconomy) ||
        torpedoes.LoadedCount(Game::Armament::PlayerWeaponType::HeavyweightTorpedo) != 17U ||
        torpedoes.LoadedCount(Game::Armament::PlayerWeaponType::Type6576AFast) != 9U ||
        std::abs(torpedoes.ExpendedMassKg() - 6'500.0F) > 1.0F)
        return false;

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

    const auto exposedMast = ObserveExposedPeriscopeMast(
        ExposedPeriscopeMastDetectionConfig{},
        raisedPeriscope,
        periscopeOwnship,
        10.0F,
        0.0F,
        {.x = 3'000.0F, .y = 0.0F, .z = 0.0F},
        0.97);
    const auto stowedMast = ObserveExposedPeriscopeMast(
        ExposedPeriscopeMastDetectionConfig{},
        PeriscopeState{},
        periscopeOwnship,
        10.0F,
        0.0F,
        {.x = 3'000.0F, .y = 0.0F, .z = 0.0F},
        0.98);
    if (!exposedMast || !exposedMast->has_value() || !stowedMast || stowedMast->has_value() ||
        (*exposedMast)->modality != Perception::SensorModality::Optical ||
        (*exposedMast)->estimatedRangeMeters.has_value() || (*exposedMast)->classificationEvidence.has_value() ||
        (*exposedMast)->opticalIdentificationLevel != Perception::OpticalIdentificationLevel::Detected)
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
