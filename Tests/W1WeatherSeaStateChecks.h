#pragma once

#include "Simulation/Environment/WeatherSeaState.h"

#include <cmath>
#include <cstdint>
#include <limits>

namespace DeepRun::Tests
{
[[nodiscard]] inline bool RunW1WeatherSeaStateChecks()
{
    using Environment::WeatherState;
    using Environment::WeatherStateConfig;
    using Environment::WeatherStateErrorCode;
    using Environment::WindSeaState;
    using Environment::SwellState;

    float previousWind = -1.0F;
    float previousGust = -1.0F;
    float previousHs = -1.0F;
    float previousCloud = -1.0F;
    float previousRain = -1.0F;
    float previousLightning = -1.0F;
    float previousVisibility = std::numeric_limits<float>::infinity();
    for (std::uint8_t force = 0U; force <= 12U; ++force)
    {
        const auto preset = WeatherState::FullyDevelopedBeaufort(force, 450.0F, 0xA11CEULL + force);
        if (!preset)
        {
            return false;
        }
        const auto& config = preset->Config();
        if (config.beaufortForce != force || config.windDirectionDegrees != 90.0F ||
            config.windSea.meanDirectionDegrees != 90.0F || config.weatherSeed != 0xA11CEULL + force ||
            config.windSpeedMetersPerSecond < previousWind ||
            config.windGustSpeedMetersPerSecond < config.windSpeedMetersPerSecond ||
            config.windGustSpeedMetersPerSecond < previousGust ||
            config.windSea.significantWaveHeightMeters < previousHs ||
            config.cloudCoverFraction < previousCloud ||
            config.rainRateMillimetersPerHour < previousRain ||
            config.lightningRatePerMinute < previousLightning ||
            config.meteorologicalVisibilityMeters > previousVisibility)
        {
            return false;
        }
        if (config.windSea.significantWaveHeightMeters > 0.0F && config.windSea.peakPeriodSeconds <= 0.0F)
        {
            return false;
        }
        previousWind = config.windSpeedMetersPerSecond;
        previousGust = config.windGustSpeedMetersPerSecond;
        previousHs = config.windSea.significantWaveHeightMeters;
        previousCloud = config.cloudCoverFraction;
        previousRain = config.rainRateMillimetersPerHour;
        previousLightning = config.lightningRatePerMinute;
        previousVisibility = config.meteorologicalVisibilityMeters;
    }

    const auto calm = WeatherState::FullyDevelopedBeaufort(0U, 0.0F, 0U);
    const auto forceSix = WeatherState::FullyDevelopedBeaufort(6U, 0.0F, 6U);
    const auto forceEight = WeatherState::FullyDevelopedBeaufort(8U, 0.0F, 8U);
    const auto forceTen = WeatherState::FullyDevelopedBeaufort(10U, 0.0F, 10U);
    const auto forceTwelve = WeatherState::FullyDevelopedBeaufort(12U, 0.0F, 12U);
    if (!calm || !forceSix || !forceEight || !forceTen || !forceTwelve ||
        calm->Config().windSpeedMetersPerSecond != 0.0F ||
        calm->Config().windGustSpeedMetersPerSecond != 0.0F ||
        calm->Config().cloudCoverFraction != 0.0F || calm->HasRain() || calm->HasLightning() ||
        calm->Config().meteorologicalVisibilityMeters != 100000.0F ||
        forceSix->Config().windSpeedMetersPerSecond != 12.0F ||
        forceSix->Config().windSea.significantWaveHeightMeters != 3.0F ||
        forceSix->Config().windSea.probableMaximumWaveHeightMeters != 4.0F ||
        forceEight->Config().windSpeedMetersPerSecond != 19.0F ||
        forceEight->Config().windGustSpeedMetersPerSecond != 24.0F ||
        forceEight->Config().windSea.significantWaveHeightMeters != 5.5F ||
        forceEight->Config().windSea.probableMaximumWaveHeightMeters != 7.5F ||
        forceEight->Config().cloudCoverFraction < 0.9F ||
        forceEight->Config().rainRateMillimetersPerHour < 8.0F ||
        forceEight->Config().meteorologicalVisibilityMeters > 15000.0F ||
        !forceEight->HasRain() || !forceEight->HasLightning() ||
        forceTen->Config().windSea.significantWaveHeightMeters != 9.0F ||
        forceTen->Config().windSea.probableMaximumWaveHeightMeters != 12.5F ||
        forceTwelve->Config().windSea.significantWaveHeightMeters != 14.0F ||
        forceTwelve->Config().windSea.probableMaximumWaveHeightMeters != 0.0F)
    {
        return false;
    }

    const auto invalidForce = WeatherState::FullyDevelopedBeaufort(13U, 0.0F, 0U);
    if (invalidForce || invalidForce.error().code != WeatherStateErrorCode::InvalidBeaufortForce)
    {
        return false;
    }

    WeatherStateConfig authored{
        .beaufortForce = 7U,
        .windSpeedMetersPerSecond = 15.0F,
        .windGustSpeedMetersPerSecond = 19.0F,
        .windDirectionDegrees = -10.0F,
        .windSea = WindSeaState{
            .significantWaveHeightMeters = 3.0F,
            .probableMaximumWaveHeightMeters = 5.0F,
            .peakPeriodSeconds = 7.5F,
            .meanDirectionDegrees = -10.0F,
            .directionalSpreadDegrees = 38.0F},
        .swell = SwellState{
            .significantWaveHeightMeters = 4.0F,
            .peakPeriodSeconds = 12.0F,
            .meanDirectionDegrees = 725.0F,
            .directionalSpreadDegrees = 12.0F},
        .rainRateMillimetersPerHour = 18.0F,
        .meteorologicalVisibilityMeters = 1400.0F,
        .cloudCoverFraction = 0.92F,
        .lightningRatePerMinute = 0.4F,
        .weatherSeed = 0xDEADBEEFULL};
    const auto custom = WeatherState::Create(authored);
    if (!custom || custom->Config().windDirectionDegrees != 350.0F ||
        custom->Config().windSea.meanDirectionDegrees != 350.0F ||
        custom->Config().swell.meanDirectionDegrees != 5.0F ||
        std::abs(custom->CombinedSignificantWaveHeightMeters() - 5.0F) > 1.0e-5F ||
        !custom->HasRain() || !custom->HasLightning())
    {
        return false;
    }

    auto invalid = authored;
    invalid.windGustSpeedMetersPerSecond = 14.0F;
    const auto invalidWind = WeatherState::Create(invalid);
    if (invalidWind || invalidWind.error().code != WeatherStateErrorCode::InvalidWind)
    {
        return false;
    }

    invalid = authored;
    invalid.windSea.probableMaximumWaveHeightMeters = 2.0F;
    const auto invalidWindSea = WeatherState::Create(invalid);
    if (invalidWindSea || invalidWindSea.error().code != WeatherStateErrorCode::InvalidWindSea)
    {
        return false;
    }

    invalid = authored;
    invalid.swell.peakPeriodSeconds = 0.0F;
    const auto invalidSwell = WeatherState::Create(invalid);
    if (invalidSwell || invalidSwell.error().code != WeatherStateErrorCode::InvalidSwell)
    {
        return false;
    }

    invalid = authored;
    invalid.cloudCoverFraction = 1.01F;
    const auto invalidCloud = WeatherState::Create(invalid);
    if (invalidCloud || invalidCloud.error().code != WeatherStateErrorCode::InvalidAtmosphere)
    {
        return false;
    }

    invalid = authored;
    invalid.meteorologicalVisibilityMeters = 0.0F;
    const auto invalidVisibility = WeatherState::Create(invalid);
    return !invalidVisibility && invalidVisibility.error().code == WeatherStateErrorCode::InvalidAtmosphere;
}
}
