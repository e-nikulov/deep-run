#pragma once

#include "Simulation/Acoustics/AcousticDebugger.h"

#include <cmath>

namespace DeepRun::Tests
{
[[nodiscard]] inline bool RunM4AcousticDebuggerChecks()
{
    Acoustics::AcousticObservation passive{};
    passive.sensorId = "MGK540_BOW_ARRAY";
    passive.measuredBearingRadians = 0.0F;
    passive.confidence = 0.6F;

    const auto passiveDebug = Acoustics::BuildAcousticDebuggerSnapshot(
        {0.0F, 0.0F, 0.0F}, {1000.0F, 0.0F, 0.0F}, passive);
    if (!passiveDebug || std::abs(passiveDebug->groundTruthBearingRadians) > 1.0e-6F ||
        std::abs(passiveDebug->observedBearingErrorRadians) > 1.0e-6F ||
        !passiveDebug->groundTruthRangeMeters ||
        std::abs(*passiveDebug->groundTruthRangeMeters - 1000.0F) > 1.0e-4F ||
        passiveDebug->observedRangeErrorMeters.has_value() ||
        passiveDebug->estimatedTrackPositionErrorMeters.has_value())
    {
        return false;
    }

    Acoustics::AcousticObservation active = passive;
    active.kind = Acoustics::AcousticObservationKind::ActiveEcho;
    active.estimatedRangeMeters = 980.0F;
    Perception::Track estimatedTrack{};
    estimatedTrack.estimatedPositionMeters = Physics::PhysicsVector3{990.0F, 0.0F, 0.0F};
    const auto activeDebug = Acoustics::BuildAcousticDebuggerSnapshot(
        {0.0F, 0.0F, 0.0F}, {1000.0F, 0.0F, 0.0F}, active, estimatedTrack);
    if (!activeDebug || !activeDebug->observedRangeErrorMeters ||
        std::abs(*activeDebug->observedRangeErrorMeters + 20.0F) > 1.0e-4F ||
        !activeDebug->estimatedTrackPositionErrorMeters ||
        std::abs(*activeDebug->estimatedTrackPositionErrorMeters - 10.0F) > 1.0e-4F)
    {
        return false;
    }

    Acoustics::AcousticObservation invalid{};
    invalid.sensorId.clear();
    return !Acoustics::BuildAcousticDebuggerSnapshot({}, {}, invalid);
}
}
