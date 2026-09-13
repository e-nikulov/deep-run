#pragma once

#include "Game/Combat/CombatKnowledge.h"
#include "Simulation/Weapons/P700Salvo.h"

#include <array>
#include <cmath>

namespace DeepRun::Tests
{
[[nodiscard]] inline bool RunD2CombatKnowledgeSalvoChecks()
{
    using namespace DeepRun;
    using namespace DeepRun::Game::Combat;
    using namespace DeepRun::Weapons;

    const Physics::PhysicsVector3 ownship{.x = 0.0F, .y = -30.0F, .z = 0.0F};
    Perception::Track track{
        .trackId = 501U,
        .contactId = 601U,
        .lifecycle = Perception::TrackLifecycleState::Confirmed,
        .estimatedPositionMeters = std::nullopt,
        .positionUncertaintyMeters = std::nullopt,
        .estimatedVelocityMetersPerSecond = std::nullopt,
        .estimatedBearingRadians = 0.15F,
        .bearingUncertaintyRadians = 0.08F,
        .confidence = 0.62F,
        .observationCount = 2U,
        .firstObservationTimeSeconds = 1.0,
        .lastObservationTimeSeconds = 2.0,
        .classification = Perception::ContactClassification::Unknown,
        .visuallyIdentified = false,
        .opticalIdentificationLevel = Perception::OpticalIdentificationLevel::None};

    const auto bearingOnly = BuildContactHypothesis(track, ownship);
    if (!bearingOnly || bearingOnly->knowledge != ContactKnowledgeLevel::BearingOnly ||
        bearingOnly->estimatedPositionMeters || bearingOnly->estimatedRangeMeters)
    {
        return false;
    }

    track.estimatedPositionMeters = Physics::PhysicsVector3{.x = 25'000.0F, .y = 0.0F, .z = 0.0F};
    track.positionUncertaintyMeters = 480.0F;
    const auto area = BuildContactHypothesis(track, ownship);
    if (!area || area->knowledge != ContactKnowledgeLevel::AreaEstimate ||
        !area->estimatedRangeMeters || !area->hypothesisRadiusMeters ||
        std::abs(*area->hypothesisRadiusMeters - 480.0F) > 1.0e-4F)
    {
        return false;
    }

    track.classification = Perception::ContactClassification::MilitarySurfaceCombatant;
    track.visuallyIdentified = true;
    track.opticalIdentificationLevel = Perception::OpticalIdentificationLevel::TypeResolved;
    const auto classified = BuildContactHypothesis(track, ownship);
    if (!classified || classified->knowledge != ContactKnowledgeLevel::Classified)
    {
        return false;
    }
    track.opticalIdentificationLevel = Perception::OpticalIdentificationLevel::FlagOrMarkingsResolved;
    const auto identified = BuildContactHypothesis(track, ownship);
    if (!identified || identified->knowledge != ContactKnowledgeLevel::PositiveIdentification)
    {
        return false;
    }

    track.classification = Perception::ContactClassification::Unknown;
    track.visuallyIdentified = false;
    track.opticalIdentificationLevel = Perception::OpticalIdentificationLevel::None;
    track.lifecycle = Perception::TrackLifecycleState::Coasting;
    const auto coasting = BuildContactHypothesis(track, ownship);
    if (!coasting || coasting->knowledge != ContactKnowledgeLevel::Coasting)
    {
        return false;
    }

    Perception::Track hostile = track;
    hostile.lifecycle = Perception::TrackLifecycleState::Confirmed;
    hostile.estimatedPositionMeters.reset();
    hostile.positionUncertaintyMeters.reset();
    hostile.confidence = 0.40F;
    hostile.bearingUncertaintyRadians = 0.20F;
    std::array hostileTracks{hostile};
    if (AssessHiddenHostileAwareness(hostileTracks) != HostileAwarenessLevel::Suspected)
    {
        return false;
    }
    hostileTracks[0].estimatedPositionMeters = ownship;
    hostileTracks[0].positionUncertaintyMeters = 800.0F;
    hostileTracks[0].confidence = 0.60F;
    if (AssessHiddenHostileAwareness(hostileTracks) != HostileAwarenessLevel::Localized)
    {
        return false;
    }
    hostileTracks[0].positionUncertaintyMeters = 100.0F;
    hostileTracks[0].confidence = 0.80F;
    hostileTracks[0].bearingUncertaintyRadians = 0.05F;
    if (AssessHiddenHostileAwareness(hostileTracks) != HostileAwarenessLevel::FireControlQuality)
    {
        return false;
    }

    const float torpedoExposure = ExposureDetectionProbability(ExposureSource::TorpedoLaunch, 10'000.0F);
    const float p700Exposure = ExposureDetectionProbability(ExposureSource::P700Launch, 10'000.0F);
    const float activeExposure = ExposureDetectionProbability(ExposureSource::ActiveSonarTransmission, 10'000.0F);
    if (!(p700Exposure > torpedoExposure && activeExposure > torpedoExposure && torpedoExposure > 0.0F))
    {
        return false;
    }

    std::size_t torpedoDetections = 0U;
    std::size_t p700Detections = 0U;
    const Physics::PhysicsVector3 observer{.x = 10'000.0F, .y = 0.0F, .z = 0.0F};
    for (std::uint64_t seed = 1U; seed <= 512U; ++seed)
    {
        const auto torpedoObserved = ObserveExposureEvent(
            ExposureSource::TorpedoLaunch, observer, ownship, "HOSTILE_PASSIVE", 10.0, seed);
        const auto p700Observed = ObserveExposureEvent(
            ExposureSource::P700Launch, observer, ownship, "HOSTILE_PASSIVE", 10.0, seed);
        if (!torpedoObserved || !p700Observed)
        {
            return false;
        }
        torpedoDetections += torpedoObserved->has_value() ? 1U : 0U;
        p700Detections += p700Observed->has_value() ? 1U : 0U;
        if (p700Observed->has_value() &&
            ((*p700Observed)->estimatedRangeMeters || (*p700Observed)->classificationEvidence))
        {
            return false;
        }
    }
    if (p700Detections <= torpedoDetections)
    {
        return false;
    }

    const std::array pairEvidence{
        P700SalvoObservation{
            .missileId = 1U,
            .trackId = 77U,
            .perceivedAimPointMeters = {.x = 25'000.0F, .y = 0.0F, .z = 0.0F},
            .positionUncertaintyMeters = 400.0F,
            .estimatedBearingRadians = 0.0F,
            .bearingUncertaintyRadians = 0.08F,
            .confidence = 0.75F},
        P700SalvoObservation{
            .missileId = 2U,
            .trackId = 77U,
            .perceivedAimPointMeters = {.x = 25'120.0F, .y = 20.0F, .z = 0.0F},
            .positionUncertaintyMeters = 400.0F,
            .estimatedBearingRadians = 0.01F,
            .bearingUncertaintyRadians = 0.08F,
            .confidence = 0.75F}};
    const auto fusedPair = FuseP700SalvoObservations(pairEvidence);
    if (!fusedPair || fusedPair->contributingMissiles != 2U ||
        !(fusedPair->positionUncertaintyMeters < 400.0F) ||
        !(fusedPair->bearingUncertaintyRadians < 0.08F) || fusedPair->confidence <= 0.75F)
    {
        return false;
    }

    auto conflictingEvidence = pairEvidence;
    conflictingEvidence[1].perceivedAimPointMeters.x = 40'000.0F;
    const auto fusedConflict = FuseP700SalvoObservations(conflictingEvidence);
    if (!fusedConflict || fusedConflict->contributingMissiles != 1U ||
        std::abs(fusedConflict->positionUncertaintyMeters - 400.0F) > 1.0e-3F)
    {
        return false;
    }
    auto wrongTrackEvidence = pairEvidence;
    wrongTrackEvidence[1].trackId = 78U;
    if (FuseP700SalvoObservations(wrongTrackEvidence))
    {
        return false;
    }

    const P700GranitDefinition definition{
        .weapon = WeaponDefinition{
            .id = "d2.p700-salvo",
            .preparationSeconds = 1.0,
            .targeting = WeaponTargetingRequirements{
                .minimumTrackConfidence = 0.65F,
                .maximumBearingUncertaintyRadians = 0.12F,
                .maximumPositionUncertaintyMeters = 500.0F,
                .requiresEstimatedPosition = true,
                .allowCoastingTrack = false}}};
    const Perception::Track launchTrack{
        .trackId = 77U,
        .contactId = 88U,
        .lifecycle = Perception::TrackLifecycleState::Confirmed,
        .estimatedPositionMeters = Physics::PhysicsVector3{.x = 25'000.0F, .y = 0.0F, .z = 0.0F},
        .positionUncertaintyMeters = 400.0F,
        .estimatedVelocityMetersPerSecond = std::nullopt,
        .estimatedBearingRadians = 0.0F,
        .bearingUncertaintyRadians = 0.08F,
        .confidence = 0.75F,
        .observationCount = 3U,
        .firstObservationTimeSeconds = 1.0,
        .lastObservationTimeSeconds = 4.0,
        .classification = Perception::ContactClassification::Unknown,
        .visuallyIdentified = false,
        .opticalIdentificationLevel = Perception::OpticalIdentificationLevel::None};
    constexpr float launchPitch = 0.6981317007977318F;
    const Physics::PhysicsVector3 launchForward{
        .x = static_cast<float>(std::cos(static_cast<double>(launchPitch))),
        .y = static_cast<float>(std::sin(static_cast<double>(launchPitch))),
        .z = 0.0F};
    const std::array contexts{
        P700CarrierLaunchContext{
            .launchPositionMeters = {.x = 0.0F, .y = -30.0F, .z = -6.0F},
            .launchForwardUnitVector = launchForward,
            .surfaceLevelYMeters = 0.0F,
            .launchDepthMeters = 30.0F,
            .carrierSpeedMetersPerSecond = 0.0F,
            .carrierHeadingRadians = 0.0F},
        P700CarrierLaunchContext{
            .launchPositionMeters = {.x = 2.0F, .y = -30.0F, .z = 6.0F},
            .launchForwardUnitVector = launchForward,
            .surfaceLevelYMeters = 0.0F,
            .launchDepthMeters = 30.0F,
            .carrierSpeedMetersPerSecond = 0.0F,
            .carrierHeadingRadians = 0.0F}};
    auto salvo = LaunchP700Salvo(definition, launchTrack, contexts, 5.0, 9001U);
    if (!salvo || salvo->missiles.size() != 2U || salvo->guidanceTrackId != launchTrack.trackId ||
        salvo->missiles[0].phase != P700GranitPhase::HatchOpening ||
        salvo->missiles[1].phase != P700GranitPhase::HatchOpening)
    {
        return false;
    }
    if (!UpdateP700SalvoGuidance(definition, *salvo, pairEvidence, 5.1) || !salvo->fusedTrack ||
        salvo->fusedTrack->contributingMissiles != 2U)
    {
        return false;
    }
    for (const auto& missile : salvo->missiles)
    {
        if (!missile.perceivedPositionUncertaintyMeters ||
            *missile.perceivedPositionUncertaintyMeters >= *launchTrack.positionUncertaintyMeters ||
            missile.guidanceTrackId != launchTrack.trackId)
        {
            return false;
        }
    }
    return true;
}
} // namespace DeepRun::Tests
