#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>

namespace DeepRun::Environment
{
enum class WeatherStateErrorCode
{
    InvalidBeaufortForce,
    InvalidWind,
    InvalidWindSea,
    InvalidSwell,
    InvalidAtmosphere
};

struct WeatherStateError final
{
    WeatherStateErrorCode code = WeatherStateErrorCode::InvalidAtmosphere;
    std::string message;
};

// W1 direction contract: every direction is the direction of travel in the world XZ plane,
// measured in degrees clockwise from +X. UI/import adapters may later convert meteorological
// "wind from" bearings, but Simulation never mixes the two conventions.
struct WindSeaState final
{
    float significantWaveHeightMeters = 0.0F;
    // Zero means "not supplied". Otherwise this must be >= significantWaveHeightMeters.
    float probableMaximumWaveHeightMeters = 0.0F;
    float peakPeriodSeconds = 0.0F;
    float meanDirectionDegrees = 0.0F;
    float directionalSpreadDegrees = 0.0F;
};

struct SwellState final
{
    float significantWaveHeightMeters = 0.0F;
    float peakPeriodSeconds = 0.0F;
    float meanDirectionDegrees = 0.0F;
    float directionalSpreadDegrees = 0.0F;
};

struct WeatherStateConfig final
{
    std::uint8_t beaufortForce = 0U;
    float windSpeedMetersPerSecond = 0.0F;
    float windGustSpeedMetersPerSecond = 0.0F;
    float windDirectionDegrees = 0.0F;

    WindSeaState windSea{};
    SwellState swell{};

    float rainRateMillimetersPerHour = 0.0F;
    float meteorologicalVisibilityMeters = 100000.0F;
    float cloudCoverFraction = 0.0F;
    float lightningRatePerMinute = 0.0F;

    // Stable seed consumed by future W1-B spectrum construction and weather presentation.
    // W1-A owns the value only; it does not generate waves or presentation effects.
    std::uint64_t weatherSeed = 0U;
};

class WeatherState final
{
public:
    [[nodiscard]] static std::expected<WeatherState, WeatherStateError> Create(WeatherStateConfig config)
    {
        if (config.beaufortForce > 12U)
        {
            return std::unexpected(WeatherStateError{
                WeatherStateErrorCode::InvalidBeaufortForce, "Beaufort force must be in the inclusive range 0..12"});
        }
        if (!FiniteNonNegative(config.windSpeedMetersPerSecond) ||
            !FiniteNonNegative(config.windGustSpeedMetersPerSecond) ||
            config.windGustSpeedMetersPerSecond < config.windSpeedMetersPerSecond ||
            !std::isfinite(config.windDirectionDegrees))
        {
            return std::unexpected(WeatherStateError{
                WeatherStateErrorCode::InvalidWind,
                "wind speed/gust must be finite and non-negative, gust must not be below sustained wind, and direction must be finite"});
        }
        if (!ValidateWindSea(config.windSea))
        {
            return std::unexpected(WeatherStateError{
                WeatherStateErrorCode::InvalidWindSea,
                "wind sea requires finite non-negative heights, a positive period when waves exist, an optional maximum not below Hs, and spread in 0..180 degrees"});
        }
        if (!ValidateSwell(config.swell))
        {
            return std::unexpected(WeatherStateError{
                WeatherStateErrorCode::InvalidSwell,
                "swell requires finite non-negative height, a positive period when swell exists, finite direction, and spread in 0..180 degrees"});
        }
        if (!FiniteNonNegative(config.rainRateMillimetersPerHour) ||
            !std::isfinite(config.meteorologicalVisibilityMeters) || config.meteorologicalVisibilityMeters <= 0.0F ||
            !std::isfinite(config.cloudCoverFraction) || config.cloudCoverFraction < 0.0F ||
            config.cloudCoverFraction > 1.0F || !FiniteNonNegative(config.lightningRatePerMinute))
        {
            return std::unexpected(WeatherStateError{
                WeatherStateErrorCode::InvalidAtmosphere,
                "weather atmosphere requires non-negative rain/lightning, positive finite visibility, and cloud cover in 0..1"});
        }

        config.windDirectionDegrees = NormalizeDirection(config.windDirectionDegrees);
        config.windSea.meanDirectionDegrees = NormalizeDirection(config.windSea.meanDirectionDegrees);
        config.swell.meanDirectionDegrees = NormalizeDirection(config.swell.meanDirectionDegrees);
        return WeatherState(config);
    }

    // Convenience scenario seed for a fully developed open-sea wind state. The Beaufort wind speeds and
    // probable wave heights mirror the Met Office table. Peak period and directional spread are deliberately
    // Deep Run spectral seed values rather than claims that Beaufort uniquely determines a spectrum.
    // Missions remain free to author young/limited-fetch seas and independent swell through Create().
    [[nodiscard]] static std::expected<WeatherState, WeatherStateError> FullyDevelopedBeaufort(
        const std::uint8_t force,
        const float travelDirectionDegrees,
        const std::uint64_t weatherSeed)
    {
        if (force > 12U)
        {
            return std::unexpected(WeatherStateError{
                WeatherStateErrorCode::InvalidBeaufortForce, "Beaufort force must be in the inclusive range 0..12"});
        }

        constexpr std::array<float, 13> MeanWindMetersPerSecond{
            0.0F, 1.0F, 3.0F, 5.0F, 7.0F, 10.0F, 12.0F, 15.0F, 19.0F, 23.0F, 27.0F, 31.0F, 33.0F};
        constexpr std::array<float, 13> ProbableWaveHeightMeters{
            0.0F, 0.1F, 0.2F, 0.6F, 1.0F, 2.0F, 3.0F, 4.0F, 5.5F, 7.0F, 9.0F, 11.5F, 14.0F};
        constexpr std::array<float, 13> ProbableMaximumWaveHeightMeters{
            0.0F, 0.1F, 0.3F, 1.0F, 1.5F, 2.5F, 4.0F, 5.5F, 7.5F, 10.0F, 12.5F, 16.0F, 0.0F};

        const std::size_t index = force;
        const float windSpeed = MeanWindMetersPerSecond[index];
        const float waveHeight = ProbableWaveHeightMeters[index];
        const float peakPeriod = waveHeight > 0.0F ? std::clamp(1.2F + 0.36F * windSpeed, 1.5F, 13.5F) : 0.0F;
        const float spread = force <= 2U ? 55.0F : (force <= 6U ? 42.0F : 34.0F);

        return Create(WeatherStateConfig{
            .beaufortForce = force,
            .windSpeedMetersPerSecond = windSpeed,
            .windGustSpeedMetersPerSecond = windSpeed,
            .windDirectionDegrees = travelDirectionDegrees,
            .windSea = WindSeaState{
                .significantWaveHeightMeters = waveHeight,
                .probableMaximumWaveHeightMeters = ProbableMaximumWaveHeightMeters[index],
                .peakPeriodSeconds = peakPeriod,
                .meanDirectionDegrees = travelDirectionDegrees,
                .directionalSpreadDegrees = spread},
            .swell = {},
            .rainRateMillimetersPerHour = 0.0F,
            .meteorologicalVisibilityMeters = 100000.0F,
            .cloudCoverFraction = 0.0F,
            .lightningRatePerMinute = 0.0F,
            .weatherSeed = weatherSeed});
    }

    [[nodiscard]] const WeatherStateConfig& Config() const noexcept
    {
        return config_;
    }

    [[nodiscard]] float CombinedSignificantWaveHeightMeters() const noexcept
    {
        // Independent wind sea and swell energies add in variance, therefore Hs combines by root-sum-square.
        return std::hypot(config_.windSea.significantWaveHeightMeters, config_.swell.significantWaveHeightMeters);
    }

    [[nodiscard]] bool HasRain() const noexcept
    {
        return config_.rainRateMillimetersPerHour > 0.0F;
    }

    [[nodiscard]] bool HasLightning() const noexcept
    {
        return config_.lightningRatePerMinute > 0.0F;
    }

private:
    explicit WeatherState(const WeatherStateConfig config) : config_(config) {}

    [[nodiscard]] static bool FiniteNonNegative(const float value) noexcept
    {
        return std::isfinite(value) && value >= 0.0F;
    }

    [[nodiscard]] static bool ValidSpread(const float value) noexcept
    {
        return std::isfinite(value) && value >= 0.0F && value <= 180.0F;
    }

    [[nodiscard]] static bool ValidateWindSea(const WindSeaState& state) noexcept
    {
        if (!FiniteNonNegative(state.significantWaveHeightMeters) ||
            !FiniteNonNegative(state.probableMaximumWaveHeightMeters) || !std::isfinite(state.peakPeriodSeconds) ||
            !std::isfinite(state.meanDirectionDegrees) || !ValidSpread(state.directionalSpreadDegrees))
        {
            return false;
        }
        if (state.significantWaveHeightMeters > 0.0F && state.peakPeriodSeconds <= 0.0F)
        {
            return false;
        }
        return state.probableMaximumWaveHeightMeters == 0.0F ||
               state.probableMaximumWaveHeightMeters >= state.significantWaveHeightMeters;
    }

    [[nodiscard]] static bool ValidateSwell(const SwellState& state) noexcept
    {
        if (!FiniteNonNegative(state.significantWaveHeightMeters) || !std::isfinite(state.peakPeriodSeconds) ||
            !std::isfinite(state.meanDirectionDegrees) || !ValidSpread(state.directionalSpreadDegrees))
        {
            return false;
        }
        return state.significantWaveHeightMeters <= 0.0F || state.peakPeriodSeconds > 0.0F;
    }

    [[nodiscard]] static float NormalizeDirection(const float degrees) noexcept
    {
        float normalized = std::fmod(degrees, 360.0F);
        if (normalized < 0.0F)
        {
            normalized += 360.0F;
        }
        return normalized == 360.0F ? 0.0F : normalized;
    }

    WeatherStateConfig config_{};
};
}
