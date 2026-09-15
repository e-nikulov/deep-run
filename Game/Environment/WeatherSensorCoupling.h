#pragma once

#include "Simulation/Acoustics/AcousticTypes.h"
#include "Simulation/Environment/WeatherSeaState.h"

#include <algorithm>
#include <cmath>
#include <expected>
#include <string>

namespace DeepRun::Game
{
struct WeatherPassiveAcousticResult final
{
    Acoustics::AcousticSpectrum ambientNoiseLevelDb{};
    float surfaceInfluenceFactor = 0.0F;
    float weatherStrength = 0.0F;
};

// W1-E gameplay composition for surface-generated ambient noise. WeatherState remains the environment truth;
// this Game adapter converts that truth into the coarse M4 acoustic-band vocabulary without making Acoustics
// depend on weather or making Environment depend on sensors.
//
// Baseline ambient noise is an authored scenario floor. Wind sea and rain add a bounded dB-like penalty that is
// strongest near the surface and decays continuously with receiver depth. Clouds do not affect acoustics.
[[nodiscard]] inline std::expected<WeatherPassiveAcousticResult, std::string>
EvaluateWeatherPassiveAmbientNoise(
    const Environment::WeatherState& weather,
    const float receiverSignedDepthMeters,
    const Acoustics::AcousticSpectrum& baselineAmbientNoiseLevelDb)
{
    if (!std::isfinite(receiverSignedDepthMeters) || !baselineAmbientNoiseLevelDb.IsFinite())
    {
        return std::unexpected("weather passive-acoustic inputs must be finite");
    }

    const auto& config = weather.Config();
    const float depthMeters = (std::max)(0.0F, receiverSignedDepthMeters);

    // Gameplay-scale attenuation of surface-generated noise. This is intentionally not a classified propagation
    // model: it encodes the robust behavior needed by gameplay — rough-surface noise matters most in the upper
    // ocean and becomes progressively less dominant as the receiver goes deep.
    constexpr float SurfaceNoiseDecayDepthMeters = 150.0F;
    const float surfaceInfluence = std::exp(-depthMeters / SurfaceNoiseDecayDepthMeters);

    const float normalizedWind = std::clamp(config.windSpeedMetersPerSecond / 33.0F, 0.0F, 1.0F);
    const float normalizedWave = std::clamp(weather.CombinedSignificantWaveHeightMeters() / 14.0F, 0.0F, 1.0F);
    const float normalizedRain = std::clamp(config.rainRateMillimetersPerHour / 50.0F, 0.0F, 1.0F);
    const float weatherStrength = std::clamp(0.60F * normalizedWind + 0.40F * normalizedWave, 0.0F, 1.0F);

    // Surface agitation is deliberately stronger in the medium/high coarse gameplay bands. Rain contributes
    // only to the upper bands here; it does not become a blanket low-frequency penalty. Values are bounded
    // gameplay tuning in the project's relative dB-like scale, not claimed ocean-noise measurements.
    const Acoustics::AcousticSpectrum surfaceSeaDeltaDb{
        .levelDb = {
            4.0F * weatherStrength,
            7.0F * weatherStrength,
            10.0F * weatherStrength + 2.0F * normalizedRain,
            12.0F * weatherStrength + 4.0F * normalizedRain}};

    WeatherPassiveAcousticResult result{
        .ambientNoiseLevelDb = baselineAmbientNoiseLevelDb,
        .surfaceInfluenceFactor = surfaceInfluence,
        .weatherStrength = weatherStrength};
    for (std::size_t index = 0U; index < Acoustics::AcousticBandCount; ++index)
    {
        result.ambientNoiseLevelDb.levelDb[index] += surfaceSeaDeltaDb.levelDb[index] * surfaceInfluence;
        if (!std::isfinite(result.ambientNoiseLevelDb.levelDb[index]))
        {
            return std::unexpected("weather passive-acoustic result is non-finite");
        }
    }
    return result;
}
}
