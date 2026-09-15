#pragma once

#include "Game/Environment/WeatherPresentation.h"

#include <cmath>

namespace DeepRun::Tests
{
[[nodiscard]] inline bool RunW1WeatherPresentationChecks()
{
    const auto calm = Environment::WeatherState::FullyDevelopedBeaufort(0U, 0.0F, 1U);
    if (!calm)
        return false;
    const Game::WeatherPresentationParameters calmPresentation = Game::EvaluateWeatherPresentation(*calm);
    if (!Game::ValidWeatherPresentationParameters(calmPresentation) ||
        std::abs(calmPresentation.atmosphereExtinctionPerMeter - 0.00003912F) > 1.0e-8F ||
        calmPresentation.cloudCoverFraction != 0.0F ||
        calmPresentation.precipitationFraction != 0.0F ||
        calmPresentation.sunTransmittance != 1.0F ||
        calmPresentation.skyLuminanceMultiplier != 1.0F ||
        std::abs(calmPresentation.horizonHazeFraction - 0.08F) > 1.0e-6F ||
        calmPresentation.cloudAdvection != 0.0F ||
        calmPresentation.whitecapFraction != 0.0F ||
        calmPresentation.spindriftFraction != 0.0F ||
        calmPresentation.sprayFraction != 0.0F)
    {
        return false;
    }

    // The W1-K default gale must no longer look like B8 waves under a clear summer sky. Beaufort itself does
    // not prescribe rain/clouds; these atmosphere values are the Deep Run default maritime presentation policy.
    const auto gale = Environment::WeatherState::FullyDevelopedBeaufort(8U, 0.0F, 0xB8U);
    if (!gale)
        return false;
    const Game::WeatherPresentationParameters galePresentation = Game::EvaluateWeatherPresentation(*gale);
    if (!Game::ValidWeatherPresentationParameters(galePresentation) ||
        galePresentation.cloudCoverFraction < 0.9F ||
        galePresentation.precipitationFraction < 0.60F ||
        galePresentation.sunTransmittance > 0.05F ||
        galePresentation.skyLuminanceMultiplier >= 0.5F ||
        galePresentation.horizonHazeFraction < 0.65F ||
        galePresentation.cloudAdvection <= 0.0F ||
        galePresentation.whitecapFraction < 0.80F ||
        galePresentation.spindriftFraction < 0.25F ||
        galePresentation.sprayFraction <= 0.0F ||
        galePresentation.atmosphereFogColorRgb == calmPresentation.atmosphereFogColorRgb)
    {
        return false;
    }

    const auto storm = Environment::WeatherState::FullyDevelopedBeaufort(10U, 0.0F, 0xB10U);
    if (!storm)
        return false;
    const Game::WeatherPresentationParameters stormPresentation = Game::EvaluateWeatherPresentation(*storm);
    if (!Game::ValidWeatherPresentationParameters(stormPresentation) ||
        !(stormPresentation.atmosphereExtinctionPerMeter > galePresentation.atmosphereExtinctionPerMeter) ||
        !(stormPresentation.precipitationFraction >= galePresentation.precipitationFraction) ||
        !(stormPresentation.sunTransmittance <= galePresentation.sunTransmittance) ||
        !(stormPresentation.skyLuminanceMultiplier <= galePresentation.skyLuminanceMultiplier) ||
        !(stormPresentation.horizonHazeFraction >= galePresentation.horizonHazeFraction) ||
        !(stormPresentation.whitecapFraction > galePresentation.whitecapFraction) ||
        !(stormPresentation.spindriftFraction > galePresentation.spindriftFraction) ||
        !(stormPresentation.sprayFraction > galePresentation.sprayFraction))
    {
        return false;
    }

    const auto oppositeWind = Environment::WeatherState::Create(Environment::WeatherStateConfig{
        .beaufortForce = 5U,
        .windSpeedMetersPerSecond = 10.0F,
        .windGustSpeedMetersPerSecond = 13.0F,
        .windDirectionDegrees = 180.0F,
        .windSea = Environment::WindSeaState{
            .significantWaveHeightMeters = 2.0F,
            .probableMaximumWaveHeightMeters = 2.5F,
            .peakPeriodSeconds = 4.8F,
            .meanDirectionDegrees = 180.0F,
            .directionalSpreadDegrees = 42.0F},
        .swell = {},
        .rainRateMillimetersPerHour = 0.0F,
        .meteorologicalVisibilityMeters = 20'000.0F,
        .cloudCoverFraction = 0.5F,
        .lightningRatePerMinute = 0.0F,
        .weatherSeed = 0x100U});
    const auto changedSeed = Environment::WeatherState::Create(Environment::WeatherStateConfig{
        .beaufortForce = 5U,
        .windSpeedMetersPerSecond = 10.0F,
        .windGustSpeedMetersPerSecond = 13.0F,
        .windDirectionDegrees = 180.0F,
        .windSea = Environment::WindSeaState{
            .significantWaveHeightMeters = 2.0F,
            .probableMaximumWaveHeightMeters = 2.5F,
            .peakPeriodSeconds = 4.8F,
            .meanDirectionDegrees = 180.0F,
            .directionalSpreadDegrees = 42.0F},
        .swell = {},
        .rainRateMillimetersPerHour = 0.0F,
        .meteorologicalVisibilityMeters = 20'000.0F,
        .cloudCoverFraction = 0.5F,
        .lightningRatePerMinute = 0.0F,
        .weatherSeed = 0x101U});
    if (!oppositeWind || !changedSeed)
        return false;
    const auto oppositePresentation = Game::EvaluateWeatherPresentation(*oppositeWind);
    const auto changedSeedPresentation = Game::EvaluateWeatherPresentation(*changedSeed);
    if (!(oppositePresentation.cloudAdvection < 0.0F) ||
        oppositePresentation.cloudPatternOffset == changedSeedPresentation.cloudPatternOffset)
    {
        return false;
    }

    Game::WeatherPresentationParameters invalid = calmPresentation;
    invalid.precipitationFraction = 1.1F;
    if (Game::ValidWeatherPresentationParameters(invalid))
        return false;
    invalid = calmPresentation;
    invalid.spindriftFraction = 1.1F;
    return !Game::ValidWeatherPresentationParameters(invalid);
}
} // namespace DeepRun::Tests
