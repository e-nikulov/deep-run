#pragma once

#include "Simulation/Acoustics/AcousticTypes.h"
#include "Simulation/Perception/TrackManager.h"

#include <cmath>
#include <expected>
#include <optional>
#include <string>

namespace DeepRun::Acoustics
{
// Developer-only comparison snapshot. Ground truth is intentionally admitted here and nowhere in the normal
// observation/contact/track flow. UI/AI/weapon systems must never consume this type as gameplay knowledge.
struct AcousticDebuggerSnapshot final
{
    Physics::PhysicsVector3 receiverPositionMeters{};
    Physics::PhysicsVector3 groundTruthSourcePositionMeters{};
    float groundTruthBearingRadians = 0.0F;
    float observedBearingErrorRadians = 0.0F;
    std::optional<float> groundTruthRangeMeters{};
    std::optional<float> observedRangeErrorMeters{};
    std::optional<float> estimatedTrackPositionErrorMeters{};
    AcousticObservation observation{};
    std::optional<Perception::Track> track{};
};

[[nodiscard]] inline std::expected<AcousticDebuggerSnapshot, std::string> BuildAcousticDebuggerSnapshot(
    const Physics::PhysicsVector3& receiverPositionMeters,
    const Physics::PhysicsVector3& groundTruthSourcePositionMeters,
    const AcousticObservation& observation,
    const std::optional<Perception::Track>& track = std::nullopt)
{
    if (!receiverPositionMeters.IsFinite() || !groundTruthSourcePositionMeters.IsFinite() ||
        observation.sensorId.empty() || !std::isfinite(observation.measuredBearingRadians) ||
        !std::isfinite(observation.confidence))
    {
        return std::unexpected("AcousticDebugger inputs must be finite and contain a sensor observation");
    }

    const double dx = static_cast<double>(groundTruthSourcePositionMeters.x) - receiverPositionMeters.x;
    const double dy = static_cast<double>(groundTruthSourcePositionMeters.y) - receiverPositionMeters.y;
    const double dz = static_cast<double>(groundTruthSourcePositionMeters.z) - receiverPositionMeters.z;
    const double rangeMeters = std::sqrt(dx * dx + dy * dy + dz * dz);
    const double truthBearing = std::atan2(dy, dx);
    if (!std::isfinite(rangeMeters) || !std::isfinite(truthBearing))
    {
        return std::unexpected("AcousticDebugger ground-truth geometry is non-finite");
    }

    AcousticDebuggerSnapshot result{};
    result.receiverPositionMeters = receiverPositionMeters;
    result.groundTruthSourcePositionMeters = groundTruthSourcePositionMeters;
    result.groundTruthBearingRadians = static_cast<float>(truthBearing);
    result.observedBearingErrorRadians = std::remainder(
        observation.measuredBearingRadians - result.groundTruthBearingRadians, 6.2831853F);
    result.groundTruthRangeMeters = static_cast<float>(rangeMeters);
    result.observation = observation;
    result.track = track;

    if (observation.estimatedRangeMeters)
    {
        result.observedRangeErrorMeters = *observation.estimatedRangeMeters - static_cast<float>(rangeMeters);
    }

    if (track && track->estimatedPositionMeters)
    {
        const Physics::PhysicsVector3& estimated = *track->estimatedPositionMeters;
        if (!estimated.IsFinite())
        {
            return std::unexpected("AcousticDebugger track estimate must be finite");
        }
        const double tx = static_cast<double>(estimated.x) - groundTruthSourcePositionMeters.x;
        const double ty = static_cast<double>(estimated.y) - groundTruthSourcePositionMeters.y;
        const double tz = static_cast<double>(estimated.z) - groundTruthSourcePositionMeters.z;
        const double errorMeters = std::sqrt(tx * tx + ty * ty + tz * tz);
        if (!std::isfinite(errorMeters))
        {
            return std::unexpected("AcousticDebugger track position error is non-finite");
        }
        result.estimatedTrackPositionErrorMeters = static_cast<float>(errorMeters);
    }

    return result;
}
}
