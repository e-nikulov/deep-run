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
    const Game::WeatherSensorEnvironment calmEnvironment = Game::EvaluateWeatherSensorEnvironment(*calm);
    const auto calmSurface = Game::ApplyWeatherPassiveAmbientNoise(
        calmEnvironment.passiveAcoustic, 0.0F, baseline);
    if (!Game::ValidWeatherSensorEnvironment(calmEnvironment) || !calmSurface ||
        calmSurface->ambientNoiseLevelDb != baseline || calmSurface->weatherStrength != 0.0F ||
        calmSurface->surfaceInfluenceFactor != 1.0F ||
        calmEnvironment.optical.meteorologicalVisibilityMeters != calm->Config().meteorologicalVisibilityMeters ||
        calmEnvironment.optical.ambientLightFraction != 1.0F ||
        calmEnvironment.optical.seaStateObscurationFraction != 0.0F ||
        calmEnvironment.surfaceRadar.detectionRangeMultiplier != 1.0F ||
        calmEnvironment.surfaceRadar.rangeUncertaintyMultiplier != 1.0F ||
        calmEnvironment.surfaceRadar.confidenceMultiplier != 1.0F ||
        calmEnvironment.rf.terrestrialConfidenceMultiplier != 1.0F ||
        calmEnvironment.rf.satelliteReportConfidenceMultiplier != 1.0F)
        return false;

    const auto storm = Environment::WeatherState::FullyDevelopedBeaufort(10U, 0.0F, 2U);
    if (!storm)
        return false;
    const Game::WeatherSensorEnvironment stormEnvironment = Game::EvaluateWeatherSensorEnvironment(*storm);
    const auto stormSurface = Game::ApplyWeatherPassiveAmbientNoise(
        stormEnvironment.passiveAcoustic, 0.0F, baseline);
    const auto stormAt50m = Game::ApplyWeatherPassiveAmbientNoise(
        stormEnvironment.passiveAcoustic, 50.0F, baseline);
    const auto stormDeep = Game::ApplyWeatherPassiveAmbientNoise(
        stormEnvironment.passiveAcoustic, 450.0F, baseline);
    if (!stormSurface || !stormAt50m || !stormDeep || !(stormSurface->weatherStrength > 0.0F) ||
        !(stormSurface->surfaceInfluenceFactor > stormAt50m->surfaceInfluenceFactor) ||
        !(stormAt50m->surfaceInfluenceFactor > stormDeep->surfaceInfluenceFactor) ||
        !(stormEnvironment.optical.seaStateObscurationFraction > 0.0F) ||
        !(stormEnvironment.surfaceRadar.detectionRangeMultiplier < 1.0F) ||
        !(stormEnvironment.surfaceRadar.rangeUncertaintyMultiplier > 1.0F) ||
        !(stormEnvironment.surfaceRadar.confidenceMultiplier < 1.0F))
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

    // Rain/fog/cloud are optical/radar inputs, but ordinary precipitation alone must not become a blanket
    // terrestrial-radio penalty. Rain still contributes upper-band acoustic surface noise.
    const auto rain = Environment::WeatherState::Create(Environment::WeatherStateConfig{
        .beaufortForce = 0U,
        .windSpeedMetersPerSecond = 0.0F,
        .windGustSpeedMetersPerSecond = 0.0F,
        .windDirectionDegrees = 0.0F,
        .windSea = {},
        .swell = {},
        .rainRateMillimetersPerHour = 25.0F,
        .meteorologicalVisibilityMeters = 900.0F,
        .cloudCoverFraction = 1.0F,
        .lightningRatePerMinute = 0.0F,
        .weatherSeed = 3U});
    if (!rain)
        return false;
    const Game::WeatherSensorEnvironment rainEnvironment = Game::EvaluateWeatherSensorEnvironment(*rain);
    const auto rainNoise = Game::ApplyWeatherPassiveAmbientNoise(
        rainEnvironment.passiveAcoustic, 0.0F, baseline);
    if (!rainNoise || rainNoise->ambientNoiseLevelDb.levelDb[0] != baseline.levelDb[0] ||
        rainNoise->ambientNoiseLevelDb.levelDb[1] != baseline.levelDb[1] ||
        !(rainNoise->ambientNoiseLevelDb.levelDb[2] > baseline.levelDb[2]) ||
        !((rainNoise->ambientNoiseLevelDb.levelDb[3] - baseline.levelDb[3]) >
          (rainNoise->ambientNoiseLevelDb.levelDb[2] - baseline.levelDb[2])) ||
        rainEnvironment.optical.meteorologicalVisibilityMeters != 900.0F ||
        !(rainEnvironment.optical.ambientLightFraction < 1.0F) ||
        !(rainEnvironment.optical.seaStateObscurationFraction > 0.0F) ||
        !(rainEnvironment.surfaceRadar.detectionRangeMultiplier < 1.0F) ||
        !(rainEnvironment.surfaceRadar.rangeUncertaintyMultiplier > 1.0F) ||
        rainEnvironment.rf.terrestrialConfidenceMultiplier != 1.0F ||
        rainEnvironment.rf.hostileInterceptConfidenceMultiplier != 1.0F ||
        !(rainEnvironment.rf.satelliteReportConfidenceMultiplier < 1.0F) ||
        !(rainEnvironment.rf.satelliteReportUncertaintyMultiplier > 1.0F))
        return false;

    // Lightning is the explicit generic RF-interference source in W1-E. It affects both our terrestrial ESM
    // confidence and enemy interception confidence without changing the authored RF emission itself.
    const auto thunderstorm = Environment::WeatherState::Create(Environment::WeatherStateConfig{
        .beaufortForce = 0U,
        .windSpeedMetersPerSecond = 0.0F,
        .windGustSpeedMetersPerSecond = 0.0F,
        .windDirectionDegrees = 0.0F,
        .windSea = {},
        .swell = {},
        .rainRateMillimetersPerHour = 0.0F,
        .meteorologicalVisibilityMeters = 20'000.0F,
        .cloudCoverFraction = 0.0F,
        .lightningRatePerMinute = 4.0F,
        .weatherSeed = 4U});
    if (!thunderstorm)
        return false;
    const Game::WeatherSensorEnvironment thunderEnvironment =
        Game::EvaluateWeatherSensorEnvironment(*thunderstorm);
    if (thunderEnvironment.rf.interferenceFraction != 1.0F ||
        !(thunderEnvironment.rf.terrestrialConfidenceMultiplier < 1.0F) ||
        !(thunderEnvironment.rf.hostileInterceptConfidenceMultiplier < 1.0F) ||
        !(thunderEnvironment.rf.satelliteReportConfidenceMultiplier < 1.0F) ||
        !(thunderEnvironment.rf.satelliteReportUncertaintyMultiplier > 1.0F))
        return false;

    // Above-water/negative signed depth is treated as maximum surface influence for bounded transition states.
    const auto aboveSurface = Game::ApplyWeatherPassiveAmbientNoise(
        stormEnvironment.passiveAcoustic, -2.0F, baseline);
    if (!aboveSurface || aboveSurface->surfaceInfluenceFactor != 1.0F ||
        aboveSurface->ambientNoiseLevelDb != stormSurface->ambientNoiseLevelDb)
        return false;

    Acoustics::AcousticSpectrum invalidBaseline = baseline;
    invalidBaseline.levelDb[0] = NAN;
    Game::WeatherSensorEnvironment invalidEnvironment = calmEnvironment;
    invalidEnvironment.surfaceRadar.rangeUncertaintyMultiplier = 0.5F;
    return !Game::ValidWeatherSensorEnvironment(invalidEnvironment) &&
           !Game::ApplyWeatherPassiveAmbientNoise(stormEnvironment.passiveAcoustic, 0.0F, invalidBaseline) &&
           !Game::ApplyWeatherPassiveAmbientNoise(stormEnvironment.passiveAcoustic, NAN, baseline);
}
}
