#pragma once

#include "Simulation/Perception/SensorObservation.h"
#include "Simulation/Perception/TrackManager.h"

#include <cmath>

namespace DeepRun::Tests
{
[[nodiscard]] inline bool RunM4PerceptionChecks()
{
    using namespace Perception;

    Acoustics::AcousticObservation acoustic{};
    acoustic.sensorId = "MGK540_BOW_ARRAY";
    acoustic.observationTimeSeconds = 10.0;
    acoustic.arrivalTimeSeconds = 9.9;
    acoustic.measuredBearingRadians = 0.20F;
    acoustic.bearingUncertaintyRadians = 0.08F;
    acoustic.confidence = 0.55F;

    const auto firstEvidence = FromAcousticObservation(acoustic);
    if (!firstEvidence || firstEvidence->estimatedRangeMeters.has_value() ||
        firstEvidence->sensorId != acoustic.sensorId)
    {
        return false;
    }

    auto managerResult = TrackManager::Create({});
    if (!managerResult)
    {
        return false;
    }
    TrackManager manager = std::move(*managerResult);

    const auto firstTrackId = manager.IntegrateObservation(*firstEvidence);
    if (!firstTrackId)
    {
        return false;
    }
    auto contacts = manager.Contacts();
    auto tracks = manager.Tracks();
    if (contacts.size() != 1U || tracks.size() != 1U ||
        tracks[0].lifecycle != TrackLifecycleState::Tentative ||
        tracks[0].estimatedPositionMeters.has_value() || tracks[0].estimatedVelocityMetersPerSecond.has_value())
    {
        return false;
    }

    SensorObservation secondEvidence = *firstEvidence;
    secondEvidence.observationTimeSeconds = 11.0;
    secondEvidence.measuredBearingRadians = 0.24F;
    secondEvidence.bearingUncertaintyRadians = 0.06F;
    secondEvidence.confidence = 0.70F;
    const auto secondTrackId = manager.IntegrateObservation(secondEvidence);
    if (!secondTrackId || *secondTrackId != *firstTrackId)
    {
        return false;
    }

    tracks = manager.Tracks();
    if (tracks.size() != 1U || tracks[0].lifecycle != TrackLifecycleState::Confirmed ||
        tracks[0].observationCount != 2U || tracks[0].confidence <= 0.70F ||
        tracks[0].bearingUncertaintyRadians > 0.06F)
    {
        return false;
    }
    const float confirmedConfidence = tracks[0].confidence;
    const float confirmedUncertainty = tracks[0].bearingUncertaintyRadians;

    if (!manager.AdvanceTo(17.0))
    {
        return false;
    }
    tracks = manager.Tracks();
    if (tracks[0].lifecycle != TrackLifecycleState::Coasting ||
        !(tracks[0].confidence < confirmedConfidence) ||
        !(tracks[0].bearingUncertaintyRadians > confirmedUncertainty))
    {
        return false;
    }

    if (!manager.AdvanceTo(31.0))
    {
        return false;
    }
    tracks = manager.Tracks();
    if (tracks[0].lifecycle != TrackLifecycleState::Lost || tracks[0].confidence != 0.0F)
    {
        return false;
    }

    // A materially different bearing creates independent evidence instead of associating by hidden target ID.
    auto secondManagerResult = TrackManager::Create({});
    if (!secondManagerResult)
    {
        return false;
    }
    TrackManager secondManager = std::move(*secondManagerResult);
    SensorObservation east = *firstEvidence;
    east.observationTimeSeconds = 1.0;
    east.measuredBearingRadians = 0.0F;
    SensorObservation north = east;
    north.observationTimeSeconds = 1.1;
    north.measuredBearingRadians = 1.5707963F;
    if (!secondManager.IntegrateObservation(east) || !secondManager.IntegrateObservation(north) ||
        secondManager.Contacts().size() != 2U || secondManager.Tracks().size() != 2U)
    {
        return false;
    }

    return true;
}
}
