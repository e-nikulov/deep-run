#pragma once

#include "Simulation/Environment/WeatherSeaState.h"
#include "Simulation/Marine/WaterWaveField.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <numbers>
#include <optional>
#include <span>
#include <string>

namespace DeepRun::Marine
{
namespace ProductionOceanDetail
{
inline constexpr double GravityMetersPerSecondSquared = 9.80665;
inline constexpr double TwoPi = 2.0 * std::numbers::pi;
inline constexpr float MinimumRenderableHsMeters = 0.02F;
inline constexpr float MaximumSupportedHsMeters = 16.0F;
inline constexpr double MaximumConservativeFoldoverSlope = 0.42;

struct SplitMix64 final
{
    std::uint64_t state = 0U;

    [[nodiscard]] std::uint64_t Next() noexcept
    {
        state += 0x9E3779B97F4A7C15ULL;
        std::uint64_t value = state;
        value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
        value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
        return value ^ (value >> 31U);
    }

    [[nodiscard]] double Unit() noexcept
    {
        constexpr double denominator = static_cast<double>(std::uint64_t{1} << 53U);
        return static_cast<double>(Next() >> 11U) / denominator;
    }
};

[[nodiscard]] inline double DirectionTravelSign(
    const float meanDirectionDegrees,
    const float directionalSpreadDegrees,
    SplitMix64& random) noexcept
{
    const double centered = (random.Unit() - 0.5) * static_cast<double>(directionalSpreadDegrees);
    const double radians = (static_cast<double>(meanDirectionDegrees) + centered) * std::numbers::pi / 180.0;
    return std::cos(radians) >= 0.0 ? 1.0 : -1.0;
}

[[nodiscard]] inline std::expected<void, std::string> FillBand(
    WaterWaveFieldDefinition& output,
    const std::size_t firstComponent,
    const float significantWaveHeightMeters,
    const float peakPeriodSeconds,
    const float meanDirectionDegrees,
    const float directionalSpreadDegrees,
    const std::span<const float> energyWeights,
    const std::span<const float> periodMultipliers,
    const float baseHorizontalSteepness,
    SplitMix64& random)
{
    if (energyWeights.size() != periodMultipliers.size() ||
        firstComponent + energyWeights.size() > output.components.size() ||
        significantWaveHeightMeters <= 0.0F || peakPeriodSeconds <= 0.0F)
    {
        return std::unexpected("invalid production ocean spectral band inputs");
    }

    double weightSum = 0.0;
    for (const float weight : energyWeights)
    {
        if (!std::isfinite(weight) || weight <= 0.0F)
        {
            return std::unexpected("production ocean spectral energy weights must be finite and positive");
        }
        weightSum += weight;
    }
    if (!std::isfinite(weightSum) || weightSum <= 0.0)
    {
        return std::unexpected("production ocean spectral energy weight sum is invalid");
    }

    const double totalVariance = std::pow(static_cast<double>(significantWaveHeightMeters) / 4.0, 2.0);
    const std::size_t componentCount = energyWeights.size();
    for (std::size_t localIndex = 0U; localIndex < componentCount; ++localIndex)
    {
        const double normalizedWeight = static_cast<double>(energyWeights[localIndex]) / weightSum;
        const double amplitude = std::sqrt(2.0 * totalVariance * normalizedWeight);
        const double periodJitter = 0.96 + 0.08 * random.Unit();
        const double period = static_cast<double>(peakPeriodSeconds) * periodMultipliers[localIndex] * periodJitter;
        if (!std::isfinite(amplitude) || amplitude <= 0.0 || !std::isfinite(period) || period <= 0.0)
        {
            return std::unexpected("production ocean spectral component amplitude/period is invalid");
        }

        const double wavelength = GravityMetersPerSecondSquared * period * period / TwoPi;
        const double waveNumber = TwoPi / wavelength;
        const double directionSign = DirectionTravelSign(
            meanDirectionDegrees, directionalSpreadDegrees, random);
        const double angularFrequency = directionSign * TwoPi / period;
        const double phase = random.Unit() * TwoPi;

        // The bounded Gerstner horizontal term is deliberately restrained. Every component is independently
        // capped so the total conservative foldover sum remains below 0.42 even in Beaufort-12 seas.
        const double perComponentSlopeBudget = MaximumConservativeFoldoverSlope /
                                               static_cast<double>(WaterWaveComponentCapacity);
        const double safeSteepness = amplitude * waveNumber > 0.0
            ? perComponentSlopeBudget / (amplitude * waveNumber)
            : 0.0;
        const double horizontalSteepness = (std::min)(
            static_cast<double>(baseHorizontalSteepness), safeSteepness);

        output.components[firstComponent + localIndex] = WaterWaveComponent{
            .amplitudeMeters = static_cast<float>(amplitude),
            .wavelengthMeters = static_cast<float>(wavelength),
            .angularFrequencyRadiansPerSecond = static_cast<float>(angularFrequency),
            .phaseOffsetRadians = static_cast<float>(phase),
            .horizontalSteepness = static_cast<float>(horizontalSteepness)};
    }
    return {};
}
} // namespace ProductionOceanDetail

// W1-B deterministic production spectrum. The component amplitudes preserve the W1-A significant-wave-height
// energy exactly: Hs = 4 * sqrt(sum(a_i^2 / 2)). Period jitter and phases come only from weatherSeed, making
// replay/capture deterministic. Independent swell and local wind-sea retain independent peak periods/directions.
[[nodiscard]] inline std::expected<std::optional<WaterWaveFieldDefinition>, std::string>
BuildProductionOceanWaveField(const Environment::WeatherState& weather)
{
    using namespace ProductionOceanDetail;

    const auto& config = weather.Config();
    const float combinedHs = weather.CombinedSignificantWaveHeightMeters();
    if (!std::isfinite(combinedHs))
    {
        return std::unexpected("production ocean combined significant wave height is non-finite");
    }
    if (combinedHs < MinimumRenderableHsMeters)
    {
        return std::optional<WaterWaveFieldDefinition>{};
    }
    if (combinedHs > MaximumSupportedHsMeters)
    {
        return std::unexpected("production ocean significant wave height exceeds the bounded W1-B range");
    }

    WaterWaveFieldDefinition output{};
    SplitMix64 random{config.weatherSeed ^ 0x5731425F4F434541ULL}; // "W1B_OCEA"
    const bool hasWindSea = config.windSea.significantWaveHeightMeters >= MinimumRenderableHsMeters;
    const bool hasSwell = config.swell.significantWaveHeightMeters >= MinimumRenderableHsMeters;

    if (hasWindSea && hasSwell)
    {
        constexpr std::array<float, 2> SwellWeights{0.58F, 0.42F};
        constexpr std::array<float, 2> SwellPeriods{1.08F, 0.92F};
        if (const auto filled = FillBand(
                output, 0U,
                config.swell.significantWaveHeightMeters,
                config.swell.peakPeriodSeconds,
                config.swell.meanDirectionDegrees,
                config.swell.directionalSpreadDegrees,
                SwellWeights, SwellPeriods, 0.34F, random);
            !filled)
        {
            return std::unexpected(filled.error());
        }

        constexpr std::array<float, 5> WindWeights{0.12F, 0.30F, 0.26F, 0.20F, 0.12F};
        constexpr std::array<float, 5> WindPeriods{1.30F, 1.00F, 0.82F, 0.62F, 0.42F};
        if (const auto filled = FillBand(
                output, 2U,
                config.windSea.significantWaveHeightMeters,
                config.windSea.peakPeriodSeconds,
                config.windSea.meanDirectionDegrees,
                config.windSea.directionalSpreadDegrees,
                WindWeights, WindPeriods, 0.28F, random);
            !filled)
        {
            return std::unexpected(filled.error());
        }
    }
    else
    {
        constexpr std::array<float, 7> SingleBandWeights{0.06F, 0.14F, 0.26F, 0.22F, 0.16F, 0.10F, 0.06F};
        constexpr std::array<float, 7> WindPeriods{1.45F, 1.18F, 1.00F, 0.82F, 0.66F, 0.50F, 0.36F};
        constexpr std::array<float, 7> SwellPeriods{1.20F, 1.10F, 1.00F, 0.92F, 0.84F, 0.76F, 0.68F};
        const float height = hasSwell ? config.swell.significantWaveHeightMeters
                                     : config.windSea.significantWaveHeightMeters;
        const float period = hasSwell ? config.swell.peakPeriodSeconds : config.windSea.peakPeriodSeconds;
        const float direction = hasSwell ? config.swell.meanDirectionDegrees
                                         : config.windSea.meanDirectionDegrees;
        const float spread = hasSwell ? config.swell.directionalSpreadDegrees
                                      : config.windSea.directionalSpreadDegrees;
        const auto periods = hasSwell ? std::span<const float>(SwellPeriods) : std::span<const float>(WindPeriods);
        if (const auto filled = FillBand(
                output, 0U, height, period, direction, spread,
                SingleBandWeights, periods, hasSwell ? 0.34F : 0.28F, random);
            !filled)
        {
            return std::unexpected(filled.error());
        }
    }

    return std::optional<WaterWaveFieldDefinition>{output};
}

[[nodiscard]] inline float ReconstructSignificantWaveHeightMeters(
    const WaterWaveFieldDefinition& field) noexcept
{
    double variance = 0.0;
    for (const WaterWaveComponent& component : field.components)
    {
        variance += 0.5 * static_cast<double>(component.amplitudeMeters) * component.amplitudeMeters;
    }
    return static_cast<float>(4.0 * std::sqrt((std::max)(0.0, variance)));
}
}
