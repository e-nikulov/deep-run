#include "Simulation/Marine/SurfaceImpactSystem.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace DeepRun::Marine
{
namespace
{
SurfaceImpactError MakeError(const SurfaceImpactErrorCode code, const std::string& message)
{
    return SurfaceImpactError{code, message};
}

bool IsFiniteFraction(const float value) noexcept
{
    return std::isfinite(value) && value >= 0.0F && value <= 1.0F;
}

bool IsFiniteSample(const SurfaceImpactPointSample& sample) noexcept
{
    return std::isfinite(sample.worldXMeters) && std::isfinite(sample.worldYMeters) &&
           std::isfinite(sample.worldZMeters) && std::isfinite(sample.signedDepthMeters) &&
           IsFiniteFraction(sample.submergedFraction);
}

std::expected<void, SurfaceImpactError> ValidateConfig(const SurfaceImpactConfig& config)
{
    if (!IsFiniteFraction(config.rearmSubmergedFraction) ||
        !IsFiniteFraction(config.triggerSubmergedFraction) ||
        config.rearmSubmergedFraction >= config.triggerSubmergedFraction)
    {
        return std::unexpected(MakeError(
            SurfaceImpactErrorCode::InvalidConfiguration,
            "surface-impact rearm/trigger fractions must be finite, bounded, and strictly ordered"));
    }
    if (!std::isfinite(config.minimumRelativeWettingSpeedMetersPerSecond) ||
        config.minimumRelativeWettingSpeedMetersPerSecond < 0.0F ||
        !std::isfinite(config.severeRelativeWettingSpeedMetersPerSecond) ||
        config.severeRelativeWettingSpeedMetersPerSecond <= config.minimumRelativeWettingSpeedMetersPerSecond)
    {
        return std::unexpected(MakeError(
            SurfaceImpactErrorCode::InvalidConfiguration,
            "surface-impact wetting-speed thresholds must be finite, non-negative, and strictly ordered"));
    }
    return {};
}
}

std::expected<SurfaceImpactPointAdvance, SurfaceImpactError> SurfaceImpactSystem::AdvancePoint(
    const SurfaceImpactConfig& config,
    const float waterDensityKgPerCubicMeter,
    const std::size_t pointIndex,
    const SurfaceImpactPointSample& currentPoint,
    const SurfaceImpactPointState& currentState,
    const float fixedDeltaSeconds)
{
    if (const auto valid = ValidateConfig(config); !valid)
        return std::unexpected(valid.error());

    if (!std::isfinite(waterDensityKgPerCubicMeter) || waterDensityKgPerCubicMeter <= 0.0F)
    {
        return std::unexpected(MakeError(
            SurfaceImpactErrorCode::InvalidInput,
            "surface-impact water density must be finite and strictly positive"));
    }
    if (!std::isfinite(fixedDeltaSeconds) || fixedDeltaSeconds <= 0.0F)
    {
        return std::unexpected(MakeError(
            SurfaceImpactErrorCode::InvalidInput,
            "surface-impact fixed delta must be finite and strictly positive"));
    }
    if (!IsFiniteSample(currentPoint))
    {
        return std::unexpected(MakeError(
            SurfaceImpactErrorCode::InvalidInput,
            "surface-impact point sample must be finite and have a bounded submerged fraction"));
    }
    if (currentState.initialized &&
        (!std::isfinite(currentState.previousSignedDepthMeters) ||
         !IsFiniteFraction(currentState.previousSubmergedFraction)))
    {
        return std::unexpected(MakeError(
            SurfaceImpactErrorCode::InvalidState,
            "surface-impact previous point state is invalid"));
    }

    SurfaceImpactPointAdvance result;
    result.nextState.initialized = true;
    result.nextState.previousSignedDepthMeters = currentPoint.signedDepthMeters;
    result.nextState.previousSubmergedFraction = currentPoint.submergedFraction;

    if (!currentState.initialized)
    {
        result.nextState.armed = currentPoint.submergedFraction <= config.rearmSubmergedFraction;
        return result;
    }

    result.nextState.armed = currentState.armed;
    if (currentPoint.submergedFraction <= config.rearmSubmergedFraction)
        result.nextState.armed = true;

    const bool crossedTriggerFromBelow =
        currentState.previousSubmergedFraction < config.triggerSubmergedFraction &&
        currentPoint.submergedFraction >= config.triggerSubmergedFraction;
    if (!currentState.armed || !crossedTriggerFromBelow)
        return result;

    // One re-entry consumes the armed state even when it is gentle. A new impact cannot occur until the local
    // hull region has emerged far enough to pass the rearm threshold again.
    result.nextState.armed = false;

    const double wettingSpeed =
        (static_cast<double>(currentPoint.signedDepthMeters) - currentState.previousSignedDepthMeters) /
        static_cast<double>(fixedDeltaSeconds);
    if (!std::isfinite(wettingSpeed))
    {
        return std::unexpected(MakeError(
            SurfaceImpactErrorCode::NonFiniteResult,
            "surface-impact relative wetting speed is non-finite"));
    }
    if (wettingSpeed < static_cast<double>(config.minimumRelativeWettingSpeedMetersPerSecond))
        return result;

    const double dynamicPressure =
        0.5 * static_cast<double>(waterDensityKgPerCubicMeter) * wettingSpeed * wettingSpeed;
    const double severity = std::clamp(
        (wettingSpeed - static_cast<double>(config.minimumRelativeWettingSpeedMetersPerSecond)) /
            (static_cast<double>(config.severeRelativeWettingSpeedMetersPerSecond) -
             static_cast<double>(config.minimumRelativeWettingSpeedMetersPerSecond)),
        0.0,
        1.0);
    const double floatLimit = static_cast<double>((std::numeric_limits<float>::max)());
    if (!std::isfinite(dynamicPressure) || dynamicPressure > floatLimit || !std::isfinite(severity))
    {
        return std::unexpected(MakeError(
            SurfaceImpactErrorCode::NonFiniteResult,
            "surface-impact dynamic pressure/severity is not representable"));
    }

    result.event = SurfaceImpactEvent{
        .pointIndex = pointIndex,
        .worldXMeters = currentPoint.worldXMeters,
        .worldYMeters = currentPoint.worldYMeters,
        .worldZMeters = currentPoint.worldZMeters,
        .relativeWettingSpeedMetersPerSecond = static_cast<float>(wettingSpeed),
        .dynamicPressurePascals = static_cast<float>(dynamicPressure),
        .severity = static_cast<float>(severity)};
    return result;
}
}
