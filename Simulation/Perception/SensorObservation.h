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
    ElectronicSupport,
    SurfaceRadar,
    ExternalReport,
};

// Optical identification deliberately progresses through perceptual detail rather than becoming a binary
// "seen/not seen" truth leak. A distant periscope look may detect a silhouette without resolving vessel type;
// closer deliberate observation may resolve type/class, and only the closest/highest-quality view resolves
// flag or equivalent identifying markings.
enum class OpticalIdentificationLevel
{
    None,
    Detected,
    TypeResolved,
    FlagOrMarkingsResolved,
};

// Classification is perceived-world knowledge, not authoritative actor identity. Unknown is a valid and
// important gameplay state: a high-quality acoustic/radar/ESM solution can still leave the commander unsure
// whether a surface contact is a combatant or a civilian vessel.
enum class ContactClassification
{
    Unknown,
    MilitarySurfaceCombatant,
    CivilianSurfaceVessel,
};

// Perceived-world evidence shared above sensor-specific simulation. Deliberately contains no authoritative
// source/entity identity. Bearing-only passive/ESM observations may legitimately carry no range estimate.
// sensorPositionMeters, when present, is own-sensor/navigation state supplied by the observing participant so
// ranged evidence can be spatialized without exposing the observed source's authoritative position.
//
// classificationEvidence is accepted only from Optical observations at TypeResolved detail or better. Radar,
// ESM and external intelligence improve track geometry/provenance but never magically identify a civilian or
// military target. sourceAgeSeconds is used only by stale external reports and carries report age, not truth ID.
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
    OpticalIdentificationLevel opticalIdentificationLevel = OpticalIdentificationLevel::None;
    std::optional<ContactClassification> classificationEvidence{};
    std::optional<float> sourceAgeSeconds{};
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
        .opticalIdentificationLevel = OpticalIdentificationLevel::None,
        .classificationEvidence = std::nullopt,
        .sourceAgeSeconds = std::nullopt};
}
} // namespace DeepRun::Perception
