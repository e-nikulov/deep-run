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
        calmPresentation.cloudAdvection != 0.0F)
    {
        return false;
    }

    const auto storm = Environment::WeatherState::Create(Environment::WeatherStateConfig{
        .beaufortForce = 10U,
        .windSpeedMetersPerSecond = 27.0F,
        .windGustSpeedMetersPerSecond = 33.0F,
        .windDirectionDegrees = 0.0F,
        .windSea = Environment::WindSeaState{
            .significantWaveHeightMeters = 9.0F,
            .probableMaximumWaveHeightMeters = 12.5F,
            .peakPeriodSeconds = 10.0F,
            .meanDirectionDegrees = 0.0F,
            .directionalSpreadDegrees = 34.0F},
        .swell = {},
        .rainRateMillimetersPerHour = 50.0F,
        .meteorologicalVisibilityMeters = 1'000.0F,
        .cloudCoverFraction = 1.0F,
        .lightningRatePerMinute = 4.0F,
        .weatherSeed = 0xA51A51ULL});
    if (!storm)
        return false;
    const Game::WeatherPresentationParameters stormPresentation = Game::EvaluateWeatherPresentation(*storm);
    if (!Game::ValidWeatherPresentationParameters(stormPresentation) ||
        !(stormPresentation.atmosphereExtinctionPerMeter > calmPresentation.atmosphereExtinctionPerMeter) ||
        stormPresentation.cloudCoverFraction != 1.0F ||
        stormPresentation.precipitationFraction != 1.0F ||
        !(stormPresentation.sunTransmittance < calmPresentation.sunTransmittance) ||
        !(stormPresentation.skyLuminanceMultiplier < calmPresentation.skyLuminanceMultiplier) ||
        stormPresentation.horizonHazeFraction != 1.0F ||
        !(stormPresentation.cloudAdvection > 0.0F) ||
        stormPresentation.atmosphereFogColorRgb == calmPresentation.atmosphereFogColorRgb)
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
    return !Game::ValidWeatherPresentationParameters(invalid);
}
} // namespace DeepRun::Tests
