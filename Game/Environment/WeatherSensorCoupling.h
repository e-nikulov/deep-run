#pragma once

#include "Simulation/Acoustics/AcousticTypes.h"
#include "Simulation/Environment/WeatherSeaState.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <expected>
#include <string>

namespace DeepRun::Game
{
struct WeatherPassiveAcousticProfile final
{
    Acoustics::AcousticSpectrum surfaceNoiseDeltaDb{};
    float surfaceNoiseDecayDepthMeters = 150.0F;
    float weatherStrength = 0.0F;
};

struct WeatherOpticalSensorConditions final
{
    float meteorologicalVisibilityMeters = 100000.0F;
    float ambientLightFraction = 1.0F;
    float seaStateObscurationFraction = 0.0F;
};

struct WeatherSurfaceRadarConditions final
{
    float detectionRangeMultiplier = 1.0F;
    float rangeUncertaintyMultiplier = 1.0F;
    float confidenceMultiplier = 1.0F;
    float clutterFraction = 0.0F;
};

struct WeatherRfSensorConditions final
{
    // Generic surface RF/ESM deliberately ignores ordinary cloud and rain. Lightning is the only W1-E
    // broadband-interference source on this path; satellite-report quality additionally responds to rain.
    float interferenceFraction = 0.0F;
    float terrestrialConfidenceMultiplier = 1.0F;
    float hostileInterceptConfidenceMultiplier = 1.0F;
    float satelliteReportConfidenceMultiplier = 1.0F;
    float satelliteReportUncertaintyMultiplier = 1.0F;
};

// Game-owned adapter snapshot. Environment remains the source of truth and individual sensor implementations
// remain weather-agnostic. The numbers below are bounded gameplay tuning, not claimed exact/classified
// performance for Project 949A equipment.
struct WeatherSensorEnvironment final
{
    WeatherPassiveAcousticProfile passiveAcoustic{};
    WeatherOpticalSensorConditions optical{};
    WeatherSurfaceRadarConditions surfaceRadar{};
    WeatherRfSensorConditions rf{};
};

[[nodiscard]] inline bool ValidWeatherSensorEnvironment(const WeatherSensorEnvironment& environment) noexcept
{
    const auto finiteUnit = [](const float value) noexcept {
        return std::isfinite(value) && value >= 0.0F && value <= 1.0F;
    };
    if (!environment.passiveAcoustic.surfaceNoiseDeltaDb.IsFinite() ||
        !std::isfinite(environment.passiveAcoustic.surfaceNoiseDecayDepthMeters) ||
        environment.passiveAcoustic.surfaceNoiseDecayDepthMeters <= 0.0F ||
        !finiteUnit(environment.passiveAcoustic.weatherStrength) ||
        !std::isfinite(environment.optical.meteorologicalVisibilityMeters) ||
        environment.optical.meteorologicalVisibilityMeters <= 0.0F ||
        !finiteUnit(environment.optical.ambientLightFraction) ||
        !finiteUnit(environment.optical.seaStateObscurationFraction) ||
        !std::isfinite(environment.surfaceRadar.detectionRangeMultiplier) ||
        environment.surfaceRadar.detectionRangeMultiplier <= 0.0F ||
        environment.surfaceRadar.detectionRangeMultiplier > 1.0F ||
        !std::isfinite(environment.surfaceRadar.rangeUncertaintyMultiplier) ||
        environment.surfaceRadar.rangeUncertaintyMultiplier < 1.0F ||
        !finiteUnit(environment.surfaceRadar.confidenceMultiplier) ||
        !finiteUnit(environment.surfaceRadar.clutterFraction) ||
        !finiteUnit(environment.rf.interferenceFraction) ||
        !finiteUnit(environment.rf.terrestrialConfidenceMultiplier) ||
        !finiteUnit(environment.rf.hostileInterceptConfidenceMultiplier) ||
        !finiteUnit(environment.rf.satelliteReportConfidenceMultiplier) ||
        !std::isfinite(environment.rf.satelliteReportUncertaintyMultiplier) ||
        environment.rf.satelliteReportUncertaintyMultiplier < 1.0F)
    {
        return false;
    }
    for (const float delta : environment.passiveAcoustic.surfaceNoiseDeltaDb.levelDb)
    {
        if (delta < 0.0F)
            return false;
    }
    return true;
}

[[nodiscard]] inline WeatherSensorEnvironment EvaluateWeatherSensorEnvironment(
    const Environment::WeatherState& weather) noexcept
{
    const auto& config = weather.Config();
    const float normalizedWind = std::clamp(config.windSpeedMetersPerSecond / 33.0F, 0.0F, 1.0F);
    const float normalizedWave = std::clamp(weather.CombinedSignificantWaveHeightMeters() / 14.0F, 0.0F, 1.0F);
    const float normalizedRain = std::clamp(config.rainRateMillimetersPerHour / 50.0F, 0.0F, 1.0F);
    const float normalizedLightning = std::clamp(config.lightningRatePerMinute / 4.0F, 0.0F, 1.0F);
    const float weatherStrength = std::clamp(0.60F * normalizedWind + 0.40F * normalizedWave, 0.0F, 1.0F);

    WeatherSensorEnvironment result{};
    result.passiveAcoustic.weatherStrength = weatherStrength;
    result.passiveAcoustic.surfaceNoiseDeltaDb = Acoustics::AcousticSpectrum{
        .levelDb = {
            4.0F * weatherStrength,
            7.0F * weatherStrength,
            10.0F * weatherStrength + 2.0F * normalizedRain,
            12.0F * weatherStrength + 4.0F * normalizedRain}};

    // Visibility is authoritative weather data. Cloud/rain only dim this daytime gameplay abstraction; they do
    // not invent a night cycle. Sea obscuration represents wave crests, spray and rain on a raised optical head.
    result.optical.meteorologicalVisibilityMeters = config.meteorologicalVisibilityMeters;
    result.optical.ambientLightFraction = std::clamp(
        1.0F - 0.35F * config.cloudCoverFraction - 0.15F * normalizedRain, 0.35F, 1.0F);
    result.optical.seaStateObscurationFraction = std::clamp(
        0.45F * normalizedWave + 0.30F * normalizedWind + 0.25F * normalizedRain, 0.0F, 1.0F);

    // Surface-search radar loses useful range/precision in precipitation and a rough sea because of attenuation
    // and sea/rain clutter. These coefficients are GAME POLICY rather than a frequency-specific RADIAN model.
    const float radarStress = std::clamp(0.70F * normalizedRain + 0.30F * weatherStrength, 0.0F, 1.0F);
    result.surfaceRadar.clutterFraction = radarStress;
    result.surfaceRadar.detectionRangeMultiplier = std::clamp(1.0F - 0.42F * radarStress, 0.50F, 1.0F);
    result.surfaceRadar.rangeUncertaintyMultiplier = 1.0F + 1.75F * radarStress;
    result.surfaceRadar.confidenceMultiplier = std::clamp(1.0F - 0.35F * radarStress, 0.55F, 1.0F);

    // Ordinary cloud/rain are intentionally absent from terrestrial ESM/radio confidence. Lightning introduces
    // short-lived broadband interference in the W1-E abstraction. Earth-space targeting reports also receive a
    // rain penalty; this does not assert the actual frequency or link budget of any named historical system.
    result.rf.interferenceFraction = normalizedLightning;
    result.rf.terrestrialConfidenceMultiplier = std::clamp(1.0F - 0.25F * normalizedLightning, 0.70F, 1.0F);
    result.rf.hostileInterceptConfidenceMultiplier = std::clamp(1.0F - 0.20F * normalizedLightning, 0.75F, 1.0F);
    result.rf.satelliteReportConfidenceMultiplier = std::clamp(
        1.0F - 0.30F * normalizedRain - 0.20F * normalizedLightning, 0.45F, 1.0F);
    result.rf.satelliteReportUncertaintyMultiplier =
        1.0F + 1.50F * normalizedRain + 0.75F * normalizedLightning;
    return result;
}

struct WeatherPassiveAcousticResult final
{
    Acoustics::AcousticSpectrum ambientNoiseLevelDb{};
    float surfaceInfluenceFactor = 0.0F;
    float weatherStrength = 0.0F;
};

[[nodiscard]] inline std::expected<WeatherPassiveAcousticResult, std::string>
ApplyWeatherPassiveAmbientNoise(
    const WeatherPassiveAcousticProfile& profile,
    const float receiverSignedDepthMeters,
    const Acoustics::AcousticSpectrum& baselineAmbientNoiseLevelDb)
{
    if (!std::isfinite(receiverSignedDepthMeters) || !baselineAmbientNoiseLevelDb.IsFinite() ||
        !profile.surfaceNoiseDeltaDb.IsFinite() ||
        !std::isfinite(profile.surfaceNoiseDecayDepthMeters) || profile.surfaceNoiseDecayDepthMeters <= 0.0F ||
        !std::isfinite(profile.weatherStrength) || profile.weatherStrength < 0.0F || profile.weatherStrength > 1.0F)
    {
        return std::unexpected("weather passive-acoustic inputs must be finite and valid");
    }
    for (const float delta : profile.surfaceNoiseDeltaDb.levelDb)
    {
        if (delta < 0.0F)
            return std::unexpected("weather passive-acoustic surface noise delta must be non-negative");
    }

    const float depthMeters = (std::max)(0.0F, receiverSignedDepthMeters);
    const float surfaceInfluence = std::exp(-depthMeters / profile.surfaceNoiseDecayDepthMeters);
    WeatherPassiveAcousticResult result{
        .ambientNoiseLevelDb = baselineAmbientNoiseLevelDb,
        .surfaceInfluenceFactor = surfaceInfluence,
        .weatherStrength = profile.weatherStrength};
    for (std::size_t index = 0U; index < Acoustics::AcousticBandCount; ++index)
    {
        result.ambientNoiseLevelDb.levelDb[index] +=
            profile.surfaceNoiseDeltaDb.levelDb[index] * surfaceInfluence;
        if (!std::isfinite(result.ambientNoiseLevelDb.levelDb[index]))
            return std::unexpected("weather passive-acoustic result is non-finite");
    }
    return result;
}

// Compatibility/convenience path for callers that own WeatherState directly.
[[nodiscard]] inline std::expected<WeatherPassiveAcousticResult, std::string>
EvaluateWeatherPassiveAmbientNoise(
    const Environment::WeatherState& weather,
    const float receiverSignedDepthMeters,
    const Acoustics::AcousticSpectrum& baselineAmbientNoiseLevelDb)
{
    const WeatherSensorEnvironment environment = EvaluateWeatherSensorEnvironment(weather);
    return ApplyWeatherPassiveAmbientNoise(
        environment.passiveAcoustic, receiverSignedDepthMeters, baselineAmbientNoiseLevelDb);
}
}
