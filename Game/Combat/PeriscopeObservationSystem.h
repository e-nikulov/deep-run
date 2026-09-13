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
    float maximumIdentificationRangeMeters = 12'000.0F;
    float viewHalfAngleRadians = 5.0F * PeriscopePi / 180.0F;
    float bearingUncertaintyRadians = 0.25F * PeriscopePi / 180.0F;
    float minimumConfidence = 0.70F;
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
};

struct PeriscopePresentationSnapshot final
{
    bool withinOperatingDepth = false;
    bool raised = false;
    bool mastExposed = false;
    float viewBearingRadians = 0.0F;
    std::optional<Perception::ContactClassification> lastVisualClassification{};
};

[[nodiscard]] inline float WrapPeriscopeAngle(const float radians) noexcept
{
    return std::remainder(radians, 2.0F * PeriscopePi);
}

[[nodiscard]] inline float PeriscopeAngleDelta(const float from, const float to) noexcept
{
    return WrapPeriscopeAngle(to - from);
}

[[nodiscard]] inline std::expected<std::optional<Perception::SensorObservation>, std::string>
ObserveThroughPeriscope(
    const PeriscopeObservationConfig& config,
    const PeriscopeState& state,
    const Physics::PhysicsVector3& ownshipPositionMeters,
    const float signedDepthMeters,
    const float surfaceLevelYMeters,
    const PeriscopeTargetTruth& target,
    const double simulationTimeSeconds)
{
    if (!std::isfinite(config.maximumOperatingDepthMeters) || config.maximumOperatingDepthMeters <= 0.0F ||
        !std::isfinite(config.maximumIdentificationRangeMeters) || config.maximumIdentificationRangeMeters <= 0.0F ||
        !std::isfinite(config.viewHalfAngleRadians) || config.viewHalfAngleRadians <= 0.0F ||
        config.viewHalfAngleRadians >= PeriscopePi || !std::isfinite(config.bearingUncertaintyRadians) ||
        config.bearingUncertaintyRadians < 0.0F || !std::isfinite(config.minimumConfidence) ||
        config.minimumConfidence < 0.0F || config.minimumConfidence > 1.0F ||
        !std::isfinite(state.viewBearingRadians) || !ownshipPositionMeters.IsFinite() ||
        !std::isfinite(signedDepthMeters) || signedDepthMeters < 0.0F ||
        !std::isfinite(surfaceLevelYMeters) || !target.positionMeters.IsFinite() ||
        !std::isfinite(simulationTimeSeconds) || simulationTimeSeconds < 0.0 ||
        target.visualClassification == Perception::ContactClassification::Unknown)
    {
        return std::unexpected("invalid periscope observation input");
    }

    if (!state.raised || signedDepthMeters > config.maximumOperatingDepthMeters)
    {
        return std::optional<Perception::SensorObservation>{};
    }

    // A deployed mast's optical head is treated as being at the authoritative mean surface. This is a bounded
    // gameplay abstraction; it does not invent a physical mast length or alter the submarine rigid body.
    const Physics::PhysicsVector3 opticalPosition{
        .x = ownshipPositionMeters.x,
        .y = surfaceLevelYMeters,
        .z = ownshipPositionMeters.z};
    const float dx = target.positionMeters.x - opticalPosition.x;
    const float dy = target.positionMeters.y - opticalPosition.y;
    const float distanceMeters = std::hypot(dx, dy);
    if (!std::isfinite(distanceMeters) || distanceMeters > config.maximumIdentificationRangeMeters)
    {
        return std::optional<Perception::SensorObservation>{};
    }

    const float bearing = std::atan2(dy, dx);
    if (std::abs(PeriscopeAngleDelta(state.viewBearingRadians, bearing)) > config.viewHalfAngleRadians)
    {
        return std::optional<Perception::SensorObservation>{};
    }

    const float rangeFraction = std::clamp(distanceMeters / config.maximumIdentificationRangeMeters, 0.0F, 1.0F);
    const float confidence = std::clamp(1.0F - 0.25F * rangeFraction, config.minimumConfidence, 1.0F);
    const float rangeUncertainty = std::max(5.0F, distanceMeters * 0.01F);
    return std::optional<Perception::SensorObservation>{Perception::SensorObservation{
        .modality = Perception::SensorModality::Optical,
        .sensorId = "ANTEY_PERISCOPE_OPTICS",
        .sensorPositionMeters = opticalPosition,
        .observationTimeSeconds = simulationTimeSeconds,
        .measuredBearingRadians = bearing,
        .bearingUncertaintyRadians = config.bearingUncertaintyRadians,
        .estimatedRangeMeters = distanceMeters,
        .rangeUncertaintyMeters = rangeUncertainty,
        .confidence = confidence,
        .classificationEvidence = target.visualClassification}};
}

[[nodiscard]] inline PeriscopePresentationSnapshot BuildPeriscopePresentationSnapshot(
    const PeriscopeState& state,
    const float signedDepthMeters,
    const std::optional<Perception::ContactClassification> lastClassification = std::nullopt) noexcept
{
    const bool operatingDepth = std::isfinite(signedDepthMeters) && signedDepthMeters >= 0.0F &&
                                signedDepthMeters <= PeriscopeObservationConfig{}.maximumOperatingDepthMeters;
    return PeriscopePresentationSnapshot{
        .withinOperatingDepth = operatingDepth,
        .raised = state.raised,
        .mastExposed = state.raised && operatingDepth,
        .viewBearingRadians = state.viewBearingRadians,
        .lastVisualClassification = lastClassification};
}
} // namespace DeepRun::Game::Combat
