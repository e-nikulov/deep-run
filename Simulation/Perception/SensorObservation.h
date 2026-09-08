#pragma once

#include "Simulation/Acoustics/AcousticTypes.h"

#include <cmath>
#include <optional>
#include <string>

namespace DeepRun::Perception
{
enum class SensorModality
{
    PassiveAcoustic,
    ActiveAcoustic,
};

// Perceived-world evidence shared above sensor-specific simulation. Deliberately contains no authoritative
// source/entity identity. Bearing-only passive observations may legitimately carry no range estimate.
// sensorPositionMeters, when present, is own-sensor state supplied by the observing participant so ranged
// evidence can be spatialized without exposing the observed source's authoritative position.
struct SensorObservation final
{
    SensorModality modality = SensorModality::PassiveAcoustic;
    std::string sensorId;
    std::optional<Physics::PhysicsVector3> sensorPositionMeters{};
    double observationTimeSeconds = 0.0;
    float measuredBearingRadians = 0.0F;
    float bearingUncertaintyRadians = 0.0F;
    std::optional<float> estimatedRangeMeters{};
    std::optional<float> rangeUncertaintyMeters{};
    float confidence = 0.0F;
};

[[nodiscard]] inline std::optional<SensorObservation> FromAcousticObservation(
    const Acoustics::AcousticObservation& acoustic,
    const std::optional<Physics::PhysicsVector3> sensorPositionMeters = std::nullopt)
{
    if (acoustic.sensorId.empty() || !std::isfinite(acoustic.observationTimeSeconds) ||
        acoustic.observationTimeSeconds < 0.0 || !std::isfinite(acoustic.measuredBearingRadians) ||
        !std::isfinite(acoustic.bearingUncertaintyRadians) || acoustic.bearingUncertaintyRadians < 0.0F ||
        !std::isfinite(acoustic.confidence) || acoustic.confidence < 0.0F || acoustic.confidence > 1.0F)
    {
        return std::nullopt;
    }
    if ((sensorPositionMeters && !sensorPositionMeters->IsFinite()) ||
        (acoustic.estimatedRangeMeters && (!std::isfinite(*acoustic.estimatedRangeMeters) || *acoustic.estimatedRangeMeters < 0.0F)) ||
        (acoustic.rangeUncertaintyMeters && (!std::isfinite(*acoustic.rangeUncertaintyMeters) || *acoustic.rangeUncertaintyMeters < 0.0F)))
    {
        return std::nullopt;
    }

    return SensorObservation{
        .modality = acoustic.kind == Acoustics::AcousticObservationKind::ActiveEcho
            ? SensorModality::ActiveAcoustic
            : SensorModality::PassiveAcoustic,
        .sensorId = acoustic.sensorId,
        .sensorPositionMeters = sensorPositionMeters,
        .observationTimeSeconds = acoustic.observationTimeSeconds,
        .measuredBearingRadians = acoustic.measuredBearingRadians,
        .bearingUncertaintyRadians = acoustic.bearingUncertaintyRadians,
        .estimatedRangeMeters = acoustic.estimatedRangeMeters,
        .rangeUncertaintyMeters = acoustic.rangeUncertaintyMeters,
        .confidence = acoustic.confidence};
}
}