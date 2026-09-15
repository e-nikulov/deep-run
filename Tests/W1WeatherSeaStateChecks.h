#pragma once

#include "Simulation/Environment/WeatherSeaState.h"

#include <cmath>
#include <cstdint>

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
    float previousHs = -1.0F;
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
            config.windSea.significantWaveHeightMeters < previousHs)
        {
            return false;
        }
        if (config.windSea.significantWaveHeightMeters > 0.0F && config.windSea.peakPeriodSeconds <= 0.0F)
        {
            return false;
        }
        previousWind = config.windSpeedMetersPerSecond;
        previousHs = config.windSea.significantWaveHeightMeters;
    }

    const auto forceSix = WeatherState::FullyDevelopedBeaufort(6U, 0.0F, 6U);
    const auto forceTen = WeatherState::FullyDevelopedBeaufort(10U, 0.0F, 10U);
    const auto forceTwelve = WeatherState::FullyDevelopedBeaufort(12U, 0.0F, 12U);
    if (!forceSix || !forceTen || !forceTwelve ||
        forceSix->Config().windSpeedMetersPerSecond != 12.0F ||
        forceSix->Config().windSea.significantWaveHeightMeters != 3.0F ||
        forceSix->Config().windSea.probableMaximumWaveHeightMeters != 4.0F ||
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
