#pragma once

#include "Engine/Physics/PhysicsTypes.h"
#include "Simulation/Perception/SensorObservation.h"

#include <algorithm>
#include <cmath>
#include <expected>
#include <optional>
#include <string>

namespace DeepRun::Game::Combat
{
inline constexpr float PeriscopePi = 3.14159265358979323846F;

struct PeriscopeObservationConfig final
{
    float maximumOperatingDepthMeters = 20.0F;

    // Clear-day gameplay policy, deliberately not claimed as Project 949A classified/actual optical performance.
    // Public marine-navigation horizon geometry, meteorological visibility definitions and historical submarine
    // optics support staged perception: distant silhouette/contact, nearer type/class, closest flag/markings.
    float opticalHeadHeightAboveSurfaceMeters = 1.0F;
    float clearDayMeteorologicalVisibilityMeters = 20'000.0F;
    float maximumDetectionRangeMeters = 24'000.0F;
    float maximumTypeRecognitionRangeMeters = 10'000.0F;
    float maximumFlagRecognitionRangeMeters = 4'000.0F;

    float viewHalfAngleRadians = 5.0F * PeriscopePi / 180.0F;
    float bearingUncertaintyRadians = 0.25F * PeriscopePi / 180.0F;
    float minimumDetectionConfidence = 0.52F;
    float minimumTypeRecognitionConfidence = 0.72F;
};

// Environment-owned inputs to the optical sensor model. These are normalized gameplay abstractions rather than
// claims about a named real periscope. A later weather/time-of-day system can publish the same values without
// changing TrackManager, weapons or UI authority boundaries.
struct PeriscopeOpticalConditions final
{
    float meteorologicalVisibilityMeters = 20'000.0F;
    float ambientLightFraction = 1.0F;          // 0 = dark, 1 = clear daylight.
    float glareFraction = 0.0F;                 // 0 = none, 1 = severe glare in the viewing direction.
    float seaStateObscurationFraction = 0.0F;   // 0 = clear line of sight, 1 = severe spray/wave obscuration.
};

struct PeriscopeState final
{
    bool raised = false;
    float viewBearingRadians = 0.0F;
};

// Scenario truth is allowed only on this sensor-simulation side of the perception boundary. The returned
// SensorObservation deliberately contains no entity/body ID. Normal UI, fire control and AI receive only the
// resulting optical evidence after TrackManager association/fusion.
struct PeriscopeTargetTruth final
{
    Physics::PhysicsVector3 positionMeters{};
    Perception::ContactClassification visualClassification = Perception::ContactClassification::Unknown;
    // Height above mean sea level controls geographic visibility of upper works/masts. These are scenario visual
    // dimensions, not collision authority. A future production vessel definition may supply them per class.
    float visibleHeightAboveSurfaceMeters = 20.0F;
};

struct PeriscopePresentationSnapshot final
{
    bool withinOperatingDepth = false;
    bool raised = false;
    bool mastExposed = false;
    float viewBearingRadians = 0.0F;
};

[[nodiscard]] inline float WrapPeriscopeAngle(const float radians) noexcept
{
    return std::remainder(radians, 2.0F * PeriscopePi);
}

[[nodiscard]] inline float PeriscopeAngleDelta(const float from, const float to) noexcept
{
    return WrapPeriscopeAngle(to - from);
}

// Bowditch-style geographic range with standard refraction expressed in metres. The public nautical formula is
// approximately D[nm] = 1.17 * (sqrt(H_ft) + sqrt(h_ft)); 3.92 km is the equivalent coefficient for metre inputs.
[[nodiscard]] inline float PeriscopeGeographicRangeMeters(
    const float opticalHeadHeightMeters,
    const float targetVisibleHeightMeters) noexcept
{
    if (!std::isfinite(opticalHeadHeightMeters) || !std::isfinite(targetVisibleHeightMeters) ||
        opticalHeadHeightMeters < 0.0F || targetVisibleHeightMeters < 0.0F)
    {
        return 0.0F;
    }
    return 3'920.0F * (std::sqrt(opticalHeadHeightMeters) + std::sqrt(targetVisibleHeightMeters));
}

[[nodiscard]] inline bool ValidPeriscopeOpticalConditions(const PeriscopeOpticalConditions& conditions) noexcept
{
    return std::isfinite(conditions.meteorologicalVisibilityMeters) &&
           conditions.meteorologicalVisibilityMeters > 0.0F &&
           std::isfinite(conditions.ambientLightFraction) &&
           conditions.ambientLightFraction >= 0.0F && conditions.ambientLightFraction <= 1.0F &&
           std::isfinite(conditions.glareFraction) &&
           conditions.glareFraction >= 0.0F && conditions.glareFraction <= 1.0F &&
           std::isfinite(conditions.seaStateObscurationFraction) &&
           conditions.seaStateObscurationFraction >= 0.0F && conditions.seaStateObscurationFraction <= 1.0F;
}

[[nodiscard]] inline float PeriscopeDetectionQuality(const PeriscopeOpticalConditions& conditions) noexcept
{
    const float light = 0.45F + 0.55F * conditions.ambientLightFraction;
    const float glare = 1.0F - 0.35F * conditions.glareFraction;
    const float sea = 1.0F - 0.20F * conditions.seaStateObscurationFraction;
    return std::clamp(light * glare * sea, 0.20F, 1.0F);
}

[[nodiscard]] inline float PeriscopeDetailQuality(const PeriscopeOpticalConditions& conditions) noexcept
{
    const float light = 0.15F + 0.85F * conditions.ambientLightFraction;
    const float glare = 1.0F - 0.55F * conditions.glareFraction;
    const float sea = 1.0F - 0.35F * conditions.seaStateObscurationFraction;
    return std::clamp(light * glare * sea, 0.10F, 1.0F);
}

[[nodiscard]] inline std::expected<std::optional<Perception::SensorObservation>, std::string>
ObserveThroughPeriscope(
    const PeriscopeObservationConfig& config,
    const PeriscopeState& state,
    const Physics::PhysicsVector3& ownshipPositionMeters,
    const float signedDepthMeters,
    const float surfaceLevelYMeters,
    const PeriscopeTargetTruth& target,
    const double simulationTimeSeconds,
    const PeriscopeOpticalConditions& conditions = {})
{
    if (!std::isfinite(config.maximumOperatingDepthMeters) || config.maximumOperatingDepthMeters <= 0.0F ||
        !std::isfinite(config.opticalHeadHeightAboveSurfaceMeters) || config.opticalHeadHeightAboveSurfaceMeters < 0.0F ||
        !std::isfinite(config.clearDayMeteorologicalVisibilityMeters) || config.clearDayMeteorologicalVisibilityMeters <= 0.0F ||
        !std::isfinite(config.maximumDetectionRangeMeters) || config.maximumDetectionRangeMeters <= 0.0F ||
        !std::isfinite(config.maximumTypeRecognitionRangeMeters) || config.maximumTypeRecognitionRangeMeters <= 0.0F ||
        config.maximumTypeRecognitionRangeMeters > config.maximumDetectionRangeMeters ||
        !std::isfinite(config.maximumFlagRecognitionRangeMeters) || config.maximumFlagRecognitionRangeMeters <= 0.0F ||
        config.maximumFlagRecognitionRangeMeters > config.maximumTypeRecognitionRangeMeters ||
        !std::isfinite(config.viewHalfAngleRadians) || config.viewHalfAngleRadians <= 0.0F ||
        config.viewHalfAngleRadians >= PeriscopePi || !std::isfinite(config.bearingUncertaintyRadians) ||
        config.bearingUncertaintyRadians < 0.0F || !std::isfinite(config.minimumDetectionConfidence) ||
        config.minimumDetectionConfidence < 0.0F || config.minimumDetectionConfidence > 1.0F ||
        !std::isfinite(config.minimumTypeRecognitionConfidence) || config.minimumTypeRecognitionConfidence < 0.0F ||
        config.minimumTypeRecognitionConfidence > 1.0F || !ValidPeriscopeOpticalConditions(conditions) ||
        !std::isfinite(state.viewBearingRadians) || !ownshipPositionMeters.IsFinite() ||
        !std::isfinite(signedDepthMeters) || signedDepthMeters < 0.0F ||
        !std::isfinite(surfaceLevelYMeters) || !target.positionMeters.IsFinite() ||
        !std::isfinite(target.visibleHeightAboveSurfaceMeters) || target.visibleHeightAboveSurfaceMeters <= 0.0F ||
        !std::isfinite(simulationTimeSeconds) || simulationTimeSeconds < 0.0 ||
        target.visualClassification == Perception::ContactClassification::Unknown)
    {
        return std::unexpected("invalid periscope observation input");
    }

    if (!state.raised || signedDepthMeters > config.maximumOperatingDepthMeters)
    {
        return std::optional<Perception::SensorObservation>{};
    }

    // The optical head is a sensor-side abstraction at a small height above mean sea level; it never moves the
    // production submarine body or creates a physical mast collider.
    const Physics::PhysicsVector3 opticalPosition{
        .x = ownshipPositionMeters.x,
        .y = surfaceLevelYMeters + config.opticalHeadHeightAboveSurfaceMeters,
        .z = ownshipPositionMeters.z};
    const float dx = target.positionMeters.x - opticalPosition.x;
    const float dy = target.positionMeters.y - opticalPosition.y;
    const float distanceMeters = std::hypot(dx, dy);
    if (!std::isfinite(distanceMeters))
    {
        return std::unexpected("periscope target distance is non-finite");
    }

    const float detectionQuality = PeriscopeDetectionQuality(conditions);
    const float detailQuality = PeriscopeDetailQuality(conditions);
    const float geographicRangeMeters = PeriscopeGeographicRangeMeters(
        config.opticalHeadHeightAboveSurfaceMeters, target.visibleHeightAboveSurfaceMeters);
    const float effectiveDetectionRangeMeters = std::min({
        config.maximumDetectionRangeMeters * detectionQuality,
        config.clearDayMeteorologicalVisibilityMeters,
        conditions.meteorologicalVisibilityMeters,
        geographicRangeMeters});
    if (distanceMeters > effectiveDetectionRangeMeters)
    {
        return std::optional<Perception::SensorObservation>{};
    }

    const float bearing = std::atan2(dy, dx);
    if (std::abs(PeriscopeAngleDelta(state.viewBearingRadians, bearing)) > config.viewHalfAngleRadians)
    {
        return std::optional<Perception::SensorObservation>{};
    }

    const float typeRecognitionRangeMeters = std::min(
        effectiveDetectionRangeMeters, config.maximumTypeRecognitionRangeMeters * detailQuality);
    const float flagRecognitionRangeMeters = std::min(
        typeRecognitionRangeMeters, config.maximumFlagRecognitionRangeMeters * detailQuality);

    Perception::OpticalIdentificationLevel detail = Perception::OpticalIdentificationLevel::Detected;
    std::optional<Perception::ContactClassification> classification{};
    std::optional<float> estimatedRange{};
    std::optional<float> rangeUncertainty{};

    if (distanceMeters <= typeRecognitionRangeMeters)
    {
        detail = Perception::OpticalIdentificationLevel::TypeResolved;
        classification = target.visualClassification;
        // Once type/class is resolved, a deliberate high-power periscope observation can support a stadimeter-
        // style range estimate. Before that point the optical observation remains bearing-only.
        estimatedRange = distanceMeters;
        rangeUncertainty = std::max(25.0F, distanceMeters * 0.03F);
    }
    if (distanceMeters <= flagRecognitionRangeMeters)
    {
        detail = Perception::OpticalIdentificationLevel::FlagOrMarkingsResolved;
        rangeUncertainty = std::max(15.0F, distanceMeters * 0.02F);
    }

    const float rangeFraction = std::clamp(distanceMeters / effectiveDetectionRangeMeters, 0.0F, 1.0F);
    float confidence = std::clamp(
        (1.0F - 0.45F * rangeFraction) * (0.75F + 0.25F * detectionQuality),
        config.minimumDetectionConfidence,
        1.0F);
    if (static_cast<int>(detail) >= static_cast<int>(Perception::OpticalIdentificationLevel::TypeResolved))
    {
        confidence = std::max(confidence, config.minimumTypeRecognitionConfidence);
    }

    return std::optional<Perception::SensorObservation>{Perception::SensorObservation{
        .modality = Perception::SensorModality::Optical,
        .sensorId = "ANTEY_PERISCOPE_OPTICS",
        .sensorPositionMeters = opticalPosition,
        .observationTimeSeconds = simulationTimeSeconds,
        .measuredBearingRadians = bearing,
        .bearingUncertaintyRadians = config.bearingUncertaintyRadians,
        .estimatedRangeMeters = estimatedRange,
        .rangeUncertaintyMeters = rangeUncertainty,
        .confidence = confidence,
        .opticalIdentificationLevel = detail,
        .classificationEvidence = classification}};
}

struct ExposedPeriscopeMastDetectionConfig final
{
    // GAME POLICY for naked-eye / ordinary optical-watch detection of a tiny exposed mast in open sea.
    // Visual detection is intentionally a close-range event; the meaningful long-range penalty for RADIAN/radio
    // use comes from hostile ESM, not from granting surface ships implausible long-range eyesight. Weather, glare
    // and sea-state obscuration reduce this already-short envelope further. These are gameplay values, not TTX.
    float maximumDetectionRangeMeters = 2'500.0F;
    float bearingUncertaintyRadians = 2.5F * PeriscopePi / 180.0F;
    float minimumConfidence = 0.30F;
};

[[nodiscard]] inline std::expected<std::optional<Perception::SensorObservation>, std::string>
ObserveExposedPeriscopeMast(
    const ExposedPeriscopeMastDetectionConfig& config,
    const PeriscopeState& state,
    const Physics::PhysicsVector3& ownshipPositionMeters,
    const float signedDepthMeters,
    const float surfaceLevelYMeters,
    const Physics::PhysicsVector3& observerPositionMeters,
    const double simulationTimeSeconds,
    const PeriscopeOpticalConditions& conditions = {})
{
    if (!std::isfinite(config.maximumDetectionRangeMeters) || config.maximumDetectionRangeMeters <= 0.0F ||
        !std::isfinite(config.bearingUncertaintyRadians) || config.bearingUncertaintyRadians < 0.0F ||
        !std::isfinite(config.minimumConfidence) || config.minimumConfidence < 0.0F || config.minimumConfidence > 1.0F ||
        !ownshipPositionMeters.IsFinite() || !observerPositionMeters.IsFinite() || !std::isfinite(signedDepthMeters) ||
        signedDepthMeters < 0.0F || !std::isfinite(surfaceLevelYMeters) || !std::isfinite(simulationTimeSeconds) ||
        simulationTimeSeconds < 0.0 || !ValidPeriscopeOpticalConditions(conditions))
    {
        return std::unexpected("invalid exposed-periscope visual detection input");
    }
    if (!state.raised || signedDepthMeters > PeriscopeObservationConfig{}.maximumOperatingDepthMeters)
        return std::optional<Perception::SensorObservation>{};

    const Physics::PhysicsVector3 mastTop{
        .x = ownshipPositionMeters.x,
        .y = surfaceLevelYMeters + PeriscopeObservationConfig{}.opticalHeadHeightAboveSurfaceMeters,
        .z = ownshipPositionMeters.z};
    const float dx = mastTop.x - observerPositionMeters.x;
    const float dy = mastTop.y - observerPositionMeters.y;
    const float distanceMeters = std::hypot(dx, dy);
    const float quality = PeriscopeDetectionQuality(conditions);
    const float effectiveRange = std::min({
        config.maximumDetectionRangeMeters * quality,
        conditions.meteorologicalVisibilityMeters,
        PeriscopeObservationConfig{}.clearDayMeteorologicalVisibilityMeters});
    if (!std::isfinite(distanceMeters) || distanceMeters > effectiveRange)
        return std::optional<Perception::SensorObservation>{};

    const float rangeFraction = std::clamp(distanceMeters / effectiveRange, 0.0F, 1.0F);
    const float confidence = std::clamp(
        (1.0F - 0.35F * rangeFraction) * quality,
        config.minimumConfidence,
        1.0F);
    return std::optional<Perception::SensorObservation>{Perception::SensorObservation{
        .modality = Perception::SensorModality::Optical,
        .sensorId = "DESTROYER_VISUAL_WATCH",
        .sensorPositionMeters = observerPositionMeters,
        .observationTimeSeconds = simulationTimeSeconds,
        .measuredBearingRadians = std::atan2(dy, dx),
        .bearingUncertaintyRadians = config.bearingUncertaintyRadians,
        .estimatedRangeMeters = std::nullopt,
        .rangeUncertaintyMeters = std::nullopt,
        .confidence = confidence,
        .opticalIdentificationLevel = Perception::OpticalIdentificationLevel::Detected,
        .classificationEvidence = std::nullopt}};
}

[[nodiscard]] inline PeriscopePresentationSnapshot BuildPeriscopePresentationSnapshot(
    const PeriscopeObservationConfig& config,
    const PeriscopeState& state,
    const float signedDepthMeters) noexcept
{
    const bool operatingDepth = std::isfinite(signedDepthMeters) && signedDepthMeters >= 0.0F &&
                                signedDepthMeters <= config.maximumOperatingDepthMeters;
    return PeriscopePresentationSnapshot{
        .withinOperatingDepth = operatingDepth,
        .raised = state.raised,
        .mastExposed = state.raised && operatingDepth,
        .viewBearingRadians = state.viewBearingRadians};
}
} // namespace DeepRun::Game::Combat
