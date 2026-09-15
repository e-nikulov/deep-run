#pragma once

#include "Simulation/Environment/WeatherSeaState.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace DeepRun::Game
{
struct WeatherPresentationParameters final
{
    float atmosphereExtinctionPerMeter = 0.0F;
    std::array<float, 3> atmosphereFogColorRgb{0.390F, 0.550F, 0.720F};
    float cloudCoverFraction = 0.0F;
    float precipitationFraction = 0.0F;
    float sunTransmittance = 1.0F;
    float skyLuminanceMultiplier = 1.0F;
    float horizonHazeFraction = 0.0F;
    float cloudAdvection = 0.0F;
    float cloudPatternOffset = 0.0F;

    // Game-only visual sea descriptors derived from the Beaufort force. They do not alter WaterBody,
    // the production spectrum, buoyancy, sensors or damage. W1-K uses them to progressively expose the
    // observed Beaufort cues: white horses -> blown foam/spindrift -> airborne spray.
    float whitecapFraction = 0.0F;
    float spindriftFraction = 0.0F;
    float sprayFraction = 0.0F;
};

[[nodiscard]] inline bool ValidWeatherPresentationParameters(
    const WeatherPresentationParameters& parameters) noexcept
{
    const auto normalized = [](const float value) noexcept
    {
        return std::isfinite(value) && value >= 0.0F && value <= 1.0F;
    };
    if (!std::isfinite(parameters.atmosphereExtinctionPerMeter) ||
        parameters.atmosphereExtinctionPerMeter < 0.0F ||
        !normalized(parameters.cloudCoverFraction) ||
        !normalized(parameters.precipitationFraction) ||
        !normalized(parameters.sunTransmittance) ||
        !normalized(parameters.skyLuminanceMultiplier) ||
        !normalized(parameters.horizonHazeFraction) ||
        !std::isfinite(parameters.cloudAdvection) ||
        !normalized(parameters.cloudPatternOffset) ||
        !normalized(parameters.whitecapFraction) ||
        !normalized(parameters.spindriftFraction) ||
        !normalized(parameters.sprayFraction))
    {
        return false;
    }
    for (const float channel : parameters.atmosphereFogColorRgb)
    {
        if (!std::isfinite(channel) || channel < 0.0F)
            return false;
    }
    return true;
}

[[nodiscard]] inline WeatherPresentationParameters EvaluateWeatherPresentation(
    const Environment::WeatherState& weather) noexcept
{
    const auto& config = weather.Config();
    const float visibilityMeters = (std::max)(config.meteorologicalVisibilityMeters, 1.0F);
    // Presentation-only Koschmieder-like contrast extinction. Sensor visibility remains owned by W1-E.
    const float atmosphereExtinctionPerMeter = 3.912F / visibilityMeters;
    const float visibilitySeverity = std::clamp(
        (5.0F - std::log10(visibilityMeters)) * 0.5F, 0.0F, 1.0F);
    const float cloudCover = std::clamp(config.cloudCoverFraction, 0.0F, 1.0F);
    // Visual rain reaches a strong readable state before the physical rain-rate scale reaches an extreme
    // 50 mm/h downpour. This remains presentation-only; W1-E still consumes the exact authored mm/h value.
    const float rainFraction = std::clamp(config.rainRateMillimetersPerHour / 12.0F, 0.0F, 1.0F);
    const float stormFraction = std::clamp(static_cast<float>(config.beaufortForce) / 12.0F, 0.0F, 1.0F);

    constexpr std::array<float, 3> ClearHorizon{0.390F, 0.550F, 0.720F};
    constexpr std::array<float, 3> StormHaze{0.235F, 0.270F, 0.295F};
    const float hazeBlend = std::clamp(
        0.58F * visibilitySeverity + 0.22F * cloudCover + 0.20F * stormFraction, 0.0F, 1.0F);
    std::array<float, 3> fogColor{};
    for (std::size_t channel = 0U; channel < fogColor.size(); ++channel)
        fogColor[channel] = ClearHorizon[channel] + (StormHaze[channel] - ClearHorizon[channel]) * hazeBlend;

    constexpr float Pi = 3.14159265358979323846F;
    const float directionRadians = config.windDirectionDegrees * Pi / 180.0F;
    const float windFraction = std::clamp(config.windSpeedMetersPerSecond / 33.0F, 0.0F, 1.0F);
    const float cloudAdvection = std::cos(directionRadians) * windFraction;

    const std::uint64_t mixedSeed =
        config.weatherSeed ^ (config.weatherSeed >> 33U) ^ (config.weatherSeed << 11U);
    const float cloudPatternOffset =
        static_cast<float>(mixedSeed & 0xffffULL) / static_cast<float>(0xffffU);

    // Thick storm decks must actually hide the solar disk. The previous linear 0.85*cloud term left a very
    // bright sun visible through B8/B10 overcast. This two-factor cloud occlusion collapses transmittance near
    // full cover while preserving useful direct light through scattered/broken cloud.
    const float cloudDirectTransmittance =
        (1.0F - cloudCover) * (1.0F - 0.72F * cloudCover);
    const float sunTransmittance = std::clamp(
        cloudDirectTransmittance * (1.0F - 0.65F * rainFraction), 0.0F, 1.0F);
    const float skyLuminanceMultiplier = std::clamp(
        1.0F - 0.52F * cloudCover - 0.22F * rainFraction - 0.12F * visibilitySeverity,
        0.25F, 1.0F);
    const float horizonHazeFraction = std::clamp(
        0.08F + 0.70F * visibilitySeverity + 0.20F * cloudCover + 0.30F * rainFraction,
        0.08F, 1.0F);

    constexpr std::array<float, 13> WhitecapByBeaufort{
        0.00F, 0.00F, 0.00F, 0.06F, 0.18F, 0.35F, 0.55F, 0.70F, 0.82F, 0.90F, 0.96F, 1.00F, 1.00F};
    constexpr std::array<float, 13> SpindriftByBeaufort{
        0.00F, 0.00F, 0.00F, 0.00F, 0.00F, 0.00F, 0.00F, 0.08F, 0.30F, 0.48F, 0.68F, 0.86F, 1.00F};
    constexpr std::array<float, 13> SprayByBeaufort{
        0.00F, 0.00F, 0.00F, 0.00F, 0.00F, 0.00F, 0.00F, 0.00F, 0.08F, 0.25F, 0.50F, 0.75F, 1.00F};
    const std::size_t beaufortIndex = (std::min)(static_cast<std::size_t>(config.beaufortForce), std::size_t{12U});

    return WeatherPresentationParameters{
        .atmosphereExtinctionPerMeter = atmosphereExtinctionPerMeter,
        .atmosphereFogColorRgb = fogColor,
        .cloudCoverFraction = cloudCover,
        .precipitationFraction = rainFraction,
        .sunTransmittance = sunTransmittance,
        .skyLuminanceMultiplier = skyLuminanceMultiplier,
        .horizonHazeFraction = horizonHazeFraction,
        .cloudAdvection = cloudAdvection,
        .cloudPatternOffset = cloudPatternOffset,
        .whitecapFraction = WhitecapByBeaufort[beaufortIndex],
        .spindriftFraction = SpindriftByBeaufort[beaufortIndex],
        .sprayFraction = SprayByBeaufort[beaufortIndex]};
}
} // namespace DeepRun::Game
