#pragma once

#include "Game/Environment/WeatherSensorCoupling.h"

#include <cmath>

namespace DeepRun::Tests
{
[[nodiscard]] inline bool RunW1WeatherSensorCouplingChecks()
{
    const Acoustics::AcousticSpectrum baseline{.levelDb = {43.0F, 41.0F, 39.0F, 37.0F}};

    const auto calm = Environment::WeatherState::FullyDevelopedBeaufort(0U, 0.0F, 1U);
    if (!calm)
        return false;
    const auto calmSurface = Game::EvaluateWeatherPassiveAmbientNoise(*calm, 0.0F, baseline);
    if (!calmSurface || calmSurface->ambientNoiseLevelDb != baseline ||
        calmSurface->weatherStrength != 0.0F || calmSurface->surfaceInfluenceFactor != 1.0F)
        return false;

    const auto storm = Environment::WeatherState::FullyDevelopedBeaufort(10U, 0.0F, 2U);
    if (!storm)
        return false;
    const auto stormSurface = Game::EvaluateWeatherPassiveAmbientNoise(*storm, 0.0F, baseline);
    const auto stormAt50m = Game::EvaluateWeatherPassiveAmbientNoise(*storm, 50.0F, baseline);
    const auto stormDeep = Game::EvaluateWeatherPassiveAmbientNoise(*storm, 450.0F, baseline);
    if (!stormSurface || !stormAt50m || !stormDeep ||
        !(stormSurface->weatherStrength > 0.0F) ||
        !(stormSurface->surfaceInfluenceFactor > stormAt50m->surfaceInfluenceFactor) ||
        !(stormAt50m->surfaceInfluenceFactor > stormDeep->surfaceInfluenceFactor))
        return false;

    for (std::size_t band = 0U; band < Acoustics::AcousticBandCount; ++band)
    {
        if (!(stormSurface->ambientNoiseLevelDb.levelDb[band] > stormAt50m->ambientNoiseLevelDb.levelDb[band]) ||
            !(stormAt50m->ambientNoiseLevelDb.levelDb[band] > stormDeep->ambientNoiseLevelDb.levelDb[band]) ||
            !(stormDeep->ambientNoiseLevelDb.levelDb[band] >= baseline.levelDb[band]))
            return false;
    }
    if (!((stormSurface->ambientNoiseLevelDb.levelDb[3] - baseline.levelDb[3]) >
          (stormSurface->ambientNoiseLevelDb.levelDb[0] - baseline.levelDb[0])))
        return false;

    // Rain is an upper-band surface-noise contribution, not a blanket low-frequency or cloud penalty.
    const auto rain = Environment::WeatherState::Create(Environment::WeatherStateConfig{
        .beaufortForce = 0U,
        .windSpeedMetersPerSecond = 0.0F,
        .windGustSpeedMetersPerSecond = 0.0F,
        .windDirectionDegrees = 0.0F,
        .windSea = {},
        .swell = {},
        .rainRateMillimetersPerHour = 25.0F,
        .meteorologicalVisibilityMeters = 5000.0F,
        .cloudCoverFraction = 1.0F,
        .lightningRatePerMinute = 0.0F,
        .weatherSeed = 3U});
    if (!rain)
        return false;
    const auto rainNoise = Game::EvaluateWeatherPassiveAmbientNoise(*rain, 0.0F, baseline);
    if (!rainNoise || rainNoise->ambientNoiseLevelDb.levelDb[0] != baseline.levelDb[0] ||
        rainNoise->ambientNoiseLevelDb.levelDb[1] != baseline.levelDb[1] ||
        !(rainNoise->ambientNoiseLevelDb.levelDb[2] > baseline.levelDb[2]) ||
        !(rainNoise->ambientNoiseLevelDb.levelDb[3] > rainNoise->ambientNoiseLevelDb.levelDb[2]))
        return false;

    // Above-water/negative signed depth is treated as maximum surface influence for bounded transition states.
    const auto aboveSurface = Game::EvaluateWeatherPassiveAmbientNoise(*storm, -2.0F, baseline);
    if (!aboveSurface || aboveSurface->surfaceInfluenceFactor != 1.0F ||
        aboveSurface->ambientNoiseLevelDb != stormSurface->ambientNoiseLevelDb)
        return false;

    Acoustics::AcousticSpectrum invalidBaseline = baseline;
    invalidBaseline.levelDb[0] = NAN;
    return !Game::EvaluateWeatherPassiveAmbientNoise(*storm, 0.0F, invalidBaseline) &&
           !Game::EvaluateWeatherPassiveAmbientNoise(*storm, NAN, baseline);
}
}
