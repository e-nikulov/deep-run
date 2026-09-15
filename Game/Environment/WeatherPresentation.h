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
        !normalized(parameters.cloudPatternOffset))
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
    const float rainFraction = std::clamp(config.rainRateMillimetersPerHour / 50.0F, 0.0F, 1.0F);
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

    const float sunTransmittance = std::clamp(
        (1.0F - 0.85F * cloudCover) * (1.0F - 0.55F * rainFraction), 0.05F, 1.0F);
    const float skyLuminanceMultiplier = std::clamp(
        1.0F - 0.52F * cloudCover - 0.22F * rainFraction - 0.12F * visibilitySeverity,
        0.25F, 1.0F);
    const float horizonHazeFraction = std::clamp(
        0.08F + 0.70F * visibilitySeverity + 0.20F * cloudCover + 0.30F * rainFraction,
        0.08F, 1.0F);

    return WeatherPresentationParameters{
        .atmosphereExtinctionPerMeter = atmosphereExtinctionPerMeter,
        .atmosphereFogColorRgb = fogColor,
        .cloudCoverFraction = cloudCover,
        .precipitationFraction = rainFraction,
        .sunTransmittance = sunTransmittance,
        .skyLuminanceMultiplier = skyLuminanceMultiplier,
        .horizonHazeFraction = horizonHazeFraction,
        .cloudAdvection = cloudAdvection,
        .cloudPatternOffset = cloudPatternOffset};
}
} // namespace DeepRun::Game
