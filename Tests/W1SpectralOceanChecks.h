#pragma once

#include "Simulation/Environment/WeatherSeaState.h"
#include "Simulation/Marine/ProductionOceanSpectrum.h"
#include "Simulation/Marine/WaterBody.h"

#include <cmath>
#include <cstddef>

namespace DeepRun::Tests
{
[[nodiscard]] inline bool RunW1SpectralOceanChecks()
{
    using Environment::SwellState;
    using Environment::WeatherState;
    using Environment::WeatherStateConfig;
    using Environment::WindSeaState;

    const auto calm = WeatherState::FullyDevelopedBeaufort(0U, 0.0F, 7U);
    if (!calm)
    {
        return false;
    }
    const auto calmSpectrum = Marine::BuildProductionOceanWaveField(*calm);
    if (!calmSpectrum || calmSpectrum->has_value())
    {
        return false;
    }

    const auto moderate = WeatherState::FullyDevelopedBeaufort(5U, 25.0F, 0x12345678ULL);
    if (!moderate)
    {
        return false;
    }
    const auto first = Marine::BuildProductionOceanWaveField(*moderate);
    const auto second = Marine::BuildProductionOceanWaveField(*moderate);
    if (!first || !second || !first->has_value() || !second->has_value())
    {
        return false;
    }
    const auto& firstField = first->value();
    const auto& secondField = second->value();
    for (std::size_t index = 0U; index < firstField.components.size(); ++index)
    {
        const auto& a = firstField.components[index];
        const auto& b = secondField.components[index];
        if (a.amplitudeMeters != b.amplitudeMeters || a.wavelengthMeters != b.wavelengthMeters ||
            a.angularFrequencyRadiansPerSecond != b.angularFrequencyRadiansPerSecond ||
            a.phaseOffsetRadians != b.phaseOffsetRadians || a.horizontalSteepness != b.horizontalSteepness ||
            a.amplitudeMeters <= 0.0F || a.wavelengthMeters <= 0.0F ||
            std::abs(a.angularFrequencyRadiansPerSecond) <= 0.0F ||
            a.horizontalSteepness < 0.0F || a.horizontalSteepness > 1.0F)
        {
            return false;
        }
    }
    if (std::abs(Marine::ReconstructSignificantWaveHeightMeters(firstField) -
                 moderate->CombinedSignificantWaveHeightMeters()) > 1.0e-4F)
    {
        return false;
    }

    const auto changedSeedWeather = WeatherState::FullyDevelopedBeaufort(5U, 25.0F, 0x12345679ULL);
    if (!changedSeedWeather)
    {
        return false;
    }
    const auto changedSeed = Marine::BuildProductionOceanWaveField(*changedSeedWeather);
    if (!changedSeed || !changedSeed->has_value() ||
        changedSeed->value().components[0].phaseOffsetRadians == firstField.components[0].phaseOffsetRadians)
    {
        return false;
    }

    const auto mixedWeather = WeatherState::Create(WeatherStateConfig{
        .beaufortForce = 6U,
        .windSpeedMetersPerSecond = 12.0F,
        .windGustSpeedMetersPerSecond = 15.0F,
        .windDirectionDegrees = 15.0F,
        .windSea = WindSeaState{
            .significantWaveHeightMeters = 3.0F,
            .probableMaximumWaveHeightMeters = 4.0F,
            .peakPeriodSeconds = 6.2F,
            .meanDirectionDegrees = 15.0F,
            .directionalSpreadDegrees = 38.0F},
        .swell = SwellState{
            .significantWaveHeightMeters = 4.0F,
            .peakPeriodSeconds = 11.5F,
            .meanDirectionDegrees = 195.0F,
            .directionalSpreadDegrees = 8.0F},
        .meteorologicalVisibilityMeters = 30000.0F,
        .cloudCoverFraction = 0.45F,
        .weatherSeed = 0xBADC0FFEEULL});
    if (!mixedWeather || std::abs(mixedWeather->CombinedSignificantWaveHeightMeters() - 5.0F) > 1.0e-5F)
    {
        return false;
    }
    const auto mixedSpectrum = Marine::BuildProductionOceanWaveField(*mixedWeather);
    if (!mixedSpectrum || !mixedSpectrum->has_value() ||
        std::abs(Marine::ReconstructSignificantWaveHeightMeters(mixedSpectrum->value()) - 5.0F) > 1.0e-4F)
    {
        return false;
    }
    // Opposing swell direction must survive the 2.5D projection as opposite signed angular frequency.
    if (!(mixedSpectrum->value().components[0].angularFrequencyRadiansPerSecond < 0.0F) ||
        !(mixedSpectrum->value().components[2].angularFrequencyRadiansPerSecond > 0.0F))
    {
        return false;
    }

    const auto severe = WeatherState::FullyDevelopedBeaufort(12U, 0.0F, 12U);
    if (!severe)
    {
        return false;
    }
    const auto severeSpectrum = Marine::BuildProductionOceanWaveField(*severe);
    if (!severeSpectrum || !severeSpectrum->has_value())
    {
        return false;
    }
    const auto severeWater = Marine::WaterBody::Create({
        .surfaceLevelY = 0.0F,
        .densityKgPerCubicMeter = 1025.0F,
        .waves = severeSpectrum->value()});
    if (!severeWater)
    {
        return false;
    }
    const auto sampleA = severeWater->SampleWaveSurface({123.0F, -20.0F, 0.0F}, 17.25);
    const auto sampleB = severeWater->SampleWaveSurface({123.0F, -20.0F, 0.0F}, 17.25);
    if (!sampleA || !sampleB || sampleA->surfaceLevelY != sampleB->surfaceLevelY ||
        sampleA->surfaceNormal != sampleB->surfaceNormal || sampleA->signedDepthMeters != sampleB->signedDepthMeters ||
        sampleA->surfaceNormal.y <= 0.0F)
    {
        return false;
    }

    return true;
}
}
