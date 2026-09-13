#pragma once

#include "Game/Combat/SurfaceContactSensorTruth.h"
#include "Simulation/Weapons/P700Salvo.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <span>
#include <string>

namespace DeepRun::Game::Combat
{
// Explicit GAME POLICY for a bounded surface-target seeker abstraction. This intentionally does not claim
// classified P-700 radar parameters. Ground truth is permitted only inside this sensor-simulation boundary;
// callers receive a noisy P700SalvoObservation containing no body/entity identity.
struct P700SeekerObservationConfig final
{
    float maximumAcquisitionRangeMeters = 60'000.0F;
    float fieldOfViewHalfAngleRadians = 0.45F;
    float minimumPositionUncertaintyMeters = 70.0F;
    float fractionalPositionUncertainty = 0.018F;
    float maximumPositionUncertaintyMeters = 900.0F;
    float minimumBearingUncertaintyRadians = 0.008F;
    float maximumBearingUncertaintyRadians = 0.055F;
    float minimumConfidence = 0.58F;
    float maximumConfidence = 0.92F;
};

[[nodiscard]] inline float P700SeekerWrapAngle(const float value) noexcept
{
    return std::remainder(value, 6.28318530717958647692F);
}

[[nodiscard]] inline std::expected<std::optional<Weapons::P700SalvoObservation>, std::string>
ObserveP700SurfaceContact(
    const P700SeekerObservationConfig& config,
    const Weapons::P700GranitRuntimeState& missile,
    const std::uint64_t missileId,
    const std::uint64_t guidanceTrackId,
    const std::span<const SurfaceContactSensorTruth> surfaceTruths,
    const double simulationTimeSeconds,
    const std::uint64_t deterministicSeed)
{
    if (missileId == 0U || guidanceTrackId == 0U || !missile.positionMeters.IsFinite() ||
        !std::isfinite(missile.headingRadians) || !std::isfinite(simulationTimeSeconds) || simulationTimeSeconds < 0.0 ||
        !std::isfinite(config.maximumAcquisitionRangeMeters) || config.maximumAcquisitionRangeMeters <= 0.0F ||
        !std::isfinite(config.fieldOfViewHalfAngleRadians) || config.fieldOfViewHalfAngleRadians <= 0.0F ||
        config.fieldOfViewHalfAngleRadians > 3.1415927F ||
        !std::isfinite(config.minimumPositionUncertaintyMeters) || config.minimumPositionUncertaintyMeters <= 0.0F ||
        !std::isfinite(config.fractionalPositionUncertainty) || config.fractionalPositionUncertainty < 0.0F ||
        !std::isfinite(config.maximumPositionUncertaintyMeters) ||
        config.maximumPositionUncertaintyMeters < config.minimumPositionUncertaintyMeters ||
        !std::isfinite(config.minimumBearingUncertaintyRadians) || config.minimumBearingUncertaintyRadians <= 0.0F ||
        !std::isfinite(config.maximumBearingUncertaintyRadians) ||
        config.maximumBearingUncertaintyRadians < config.minimumBearingUncertaintyRadians ||
        !std::isfinite(config.minimumConfidence) || !std::isfinite(config.maximumConfidence) ||
        config.minimumConfidence < 0.0F || config.maximumConfidence > 1.0F ||
        config.minimumConfidence > config.maximumConfidence)
    {
        return std::unexpected("invalid P-700 seeker observation input/configuration");
    }

    const SurfaceContactSensorTruth* best = nullptr;
    float bestAngularError = config.fieldOfViewHalfAngleRadians;
    float bestRangeMeters = std::numeric_limits<float>::max();
    for (const auto& truth : surfaceTruths)
    {
        if (!truth.emitter.positionMeters.IsFinite())
        {
            continue;
        }
        const double dx = static_cast<double>(truth.emitter.positionMeters.x) - missile.positionMeters.x;
        const double dy = static_cast<double>(truth.emitter.positionMeters.y) - missile.positionMeters.y;
        const double dz = static_cast<double>(truth.emitter.positionMeters.z) - missile.positionMeters.z;
        const double range = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (!std::isfinite(range) || range > config.maximumAcquisitionRangeMeters || range <= 1.0)
        {
            continue;
        }
        const float bearing = static_cast<float>(std::atan2(dy, dx));
        const float angularError = std::abs(P700SeekerWrapAngle(bearing - missile.headingRadians));
        if (angularError > config.fieldOfViewHalfAngleRadians)
        {
            continue;
        }
        const float rangeMeters = static_cast<float>(range);
        if (best == nullptr || angularError < bestAngularError - 1.0e-5F ||
            (std::abs(angularError - bestAngularError) <= 1.0e-5F && rangeMeters < bestRangeMeters))
        {
            best = &truth;
            bestAngularError = angularError;
            bestRangeMeters = rangeMeters;
        }
    }
    if (best == nullptr)
    {
        return std::optional<Weapons::P700SalvoObservation>{};
    }

    const float normalizedRange = std::clamp(bestRangeMeters / config.maximumAcquisitionRangeMeters, 0.0F, 1.0F);
    const float positionUncertainty = std::clamp(
        std::max(config.minimumPositionUncertaintyMeters,
                 bestRangeMeters * config.fractionalPositionUncertainty),
        config.minimumPositionUncertaintyMeters,
        config.maximumPositionUncertaintyMeters);
    const float bearingUncertainty = std::lerp(
        config.minimumBearingUncertaintyRadians,
        config.maximumBearingUncertaintyRadians,
        normalizedRange);
    const float confidence = std::lerp(config.maximumConfidence, config.minimumConfidence, normalizedRange);

    // Independent seeded sensor error for each missile/sample. The truth coordinate itself never leaves this
    // function: only the perturbed perceived estimate and its declared uncertainty are returned.
    const auto symmetric = [deterministicSeed](const std::uint64_t stream) noexcept {
        return 2.0F * Weapons::P700UnitRandom(deterministicSeed, stream) - 1.0F;
    };
    const double truthDx = static_cast<double>(best->emitter.positionMeters.x) - missile.positionMeters.x;
    const double truthDy = static_cast<double>(best->emitter.positionMeters.y) - missile.positionMeters.y;
    const float truthBearing = static_cast<float>(std::atan2(truthDy, truthDx));
    const float noisyBearing = P700SeekerWrapAngle(
        truthBearing + symmetric(11U) * bearingUncertainty);
    const float rangeError = symmetric(12U) * positionUncertainty;
    const float noisyRange = std::max(1.0F, bestRangeMeters + rangeError);
    const float verticalError = symmetric(13U) * positionUncertainty * 0.20F;
    const Physics::PhysicsVector3 perceived{
        .x = missile.positionMeters.x + std::cos(noisyBearing) * noisyRange,
        .y = missile.positionMeters.y + std::sin(noisyBearing) * noisyRange,
        .z = best->emitter.positionMeters.z + verticalError};

    return std::optional<Weapons::P700SalvoObservation>{Weapons::P700SalvoObservation{
        .missileId = missileId,
        .trackId = guidanceTrackId,
        .perceivedAimPointMeters = perceived,
        .positionUncertaintyMeters = positionUncertainty,
        .estimatedBearingRadians = noisyBearing,
        .bearingUncertaintyRadians = bearingUncertainty,
        .confidence = confidence}};
}
} // namespace DeepRun::Game::Combat
