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
    Optical,
};

// Classification is perceived-world knowledge, not authoritative actor identity. Unknown is a valid and
// important gameplay state: a high-quality acoustic solution can still leave the commander unsure whether a
// surface contact is a combatant or a civilian vessel.
enum class ContactClassification
{
    Unknown,
    MilitarySurfaceCombatant,
    CivilianSurfaceVessel,
};

// Perceived-world evidence shared above sensor-specific simulation. Deliberately contains no authoritative
// source/entity identity. Bearing-only passive observations may legitimately carry no range estimate.
// sensorPositionMeters, when present, is own-sensor state supplied by the observing participant so ranged
// evidence can be spatialized without exposing the observed source's authoritative position.
//
// classificationEvidence is accepted only from Optical observations. This keeps visual identification inside
// the normal SensorObservation -> Contact -> Track knowledge path rather than leaking scenario truth into UI,
// weapon targeting or AI.
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
    std::optional<ContactClassification> classificationEvidence{};
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
        .confidence = acoustic.confidence,
        .classificationEvidence = std::nullopt};
}
}
