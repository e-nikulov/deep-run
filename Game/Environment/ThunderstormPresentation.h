#pragma once

#include "Simulation/Environment/WeatherSeaState.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

namespace DeepRun::Game
{
struct ThunderstormPresentationSample final
{
    bool hasStrike = false;
    std::uint64_t strikeIndex = 0U;
    double strikeStartPresentationTimeSeconds = 0.0;
    double thunderDuePresentationTimeSeconds = 0.0;
    float flashIntensity = 0.0F;
    float lightningViewportX = 0.5F;
    float lightningPatternOffset = 0.0F;
    float strikeDistanceMeters = 0.0F;
    float thunderGain = 0.0F;
    float thunderRumbleSeconds = 0.0F;
};

struct ThunderAudioCue final
{
    std::uint64_t strikeIndex = 0U;
    float gain = 0.0F;
    float distanceMeters = 0.0F;
    float rumbleSeconds = 0.0F;
};

namespace ThunderstormPresentationDetail
{
inline constexpr double SpeedOfSoundMetersPerSecond = 343.0;
inline constexpr double MinimumPresentationStrikeIntervalSeconds = 1.0;
inline constexpr double MaximumPresentationStrikeIntervalSeconds = 600.0;
inline constexpr double MaximumFlashAgeSeconds = 0.45;

[[nodiscard]] inline std::uint64_t SplitMix64(std::uint64_t value) noexcept
{
    value += 0x9E3779B97F4A7C15ULL;
    value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31U);
}

[[nodiscard]] inline float UnitHash(const std::uint64_t seed, const std::uint64_t stream) noexcept
{
    const std::uint64_t mixed = SplitMix64(seed ^ (stream * 0xD6E8FEB86659FD93ULL));
    constexpr double Denominator = static_cast<double>(0xFFFFFFULL);
    return static_cast<float>(static_cast<double>(mixed & 0xFFFFFFULL) / Denominator);
}

[[nodiscard]] inline double MeanStrikeIntervalSeconds(const float lightningRatePerMinute) noexcept
{
    if (!std::isfinite(lightningRatePerMinute) || lightningRatePerMinute <= 0.0F)
        return MaximumPresentationStrikeIntervalSeconds;
    return std::clamp(
        60.0 / static_cast<double>(lightningRatePerMinute),
        MinimumPresentationStrikeIntervalSeconds,
        MaximumPresentationStrikeIntervalSeconds);
}

[[nodiscard]] inline double StrikeStartSeconds(
    const std::uint64_t seed,
    const std::uint64_t strikeIndex,
    const double meanIntervalSeconds) noexcept
{
    // Bounded jitter keeps strike ordering strictly monotonic while avoiding metronomic flashes.
    const double jitter = (static_cast<double>(UnitHash(seed, strikeIndex * 7U + 1U)) - 0.5) * 0.40;
    return (static_cast<double>(strikeIndex) + 1.0 + jitter) * meanIntervalSeconds;
}

[[nodiscard]] inline float FlashPulse(const double ageSeconds) noexcept
{
    if (!std::isfinite(ageSeconds) || ageSeconds < 0.0 || ageSeconds > MaximumFlashAgeSeconds)
        return 0.0F;

    const auto gaussian = [ageSeconds](const double center, const double sigma) noexcept
    {
        const double x = (ageSeconds - center) / sigma;
        return std::exp(-0.5 * x * x);
    };
    const double flash =
        1.00 * gaussian(0.018, 0.022) +
        0.64 * gaussian(0.095, 0.032) +
        0.34 * gaussian(0.205, 0.055);
    return static_cast<float>(std::clamp(flash, 0.0, 1.0));
}
}

[[nodiscard]] inline ThunderstormPresentationSample EvaluateThunderstormPresentation(
    const Environment::WeatherState& weather,
    const double presentationTimeSeconds) noexcept
{
    ThunderstormPresentationSample sample{};
    const auto& config = weather.Config();
    if (!std::isfinite(presentationTimeSeconds) || presentationTimeSeconds < 0.0 ||
        !std::isfinite(config.lightningRatePerMinute) || config.lightningRatePerMinute <= 0.0F)
    {
        return sample;
    }

    const double meanInterval =
        ThunderstormPresentationDetail::MeanStrikeIntervalSeconds(config.lightningRatePerMinute);
    const double estimatedIndex = presentationTimeSeconds / meanInterval;
    const std::uint64_t centerIndex = estimatedIndex >= 3.0
        ? static_cast<std::uint64_t>(estimatedIndex) - 3U
        : 0U;

    bool found = false;
    double latestStrikeStart = 0.0;
    std::uint64_t latestStrikeIndex = 0U;
    // Six candidates are sufficient because jitter is bounded to +/- 20% of one strictly ordered interval.
    for (std::uint64_t offset = 0U; offset < 8U; ++offset)
    {
        const std::uint64_t strikeIndex = centerIndex + offset;
        const double strikeStart = ThunderstormPresentationDetail::StrikeStartSeconds(
            config.weatherSeed, strikeIndex, meanInterval);
        if (strikeStart <= presentationTimeSeconds && (!found || strikeStart > latestStrikeStart))
        {
            found = true;
            latestStrikeStart = strikeStart;
            latestStrikeIndex = strikeIndex;
        }
    }
    if (!found)
        return sample;

    const float distanceFraction = ThunderstormPresentationDetail::UnitHash(
        config.weatherSeed, latestStrikeIndex * 7U + 2U);
    const float strikeDistanceMeters = 1200.0F + 7800.0F * distanceFraction;
    const double thunderDue = latestStrikeStart +
        static_cast<double>(strikeDistanceMeters) / ThunderstormPresentationDetail::SpeedOfSoundMetersPerSecond;
    const float viewportX = 0.12F + 0.76F * ThunderstormPresentationDetail::UnitHash(
        config.weatherSeed, latestStrikeIndex * 7U + 3U);
    const float patternOffset = ThunderstormPresentationDetail::UnitHash(
        config.weatherSeed, latestStrikeIndex * 7U + 4U);
    const float thunderGain = std::clamp(1.14F - strikeDistanceMeters / 11'000.0F, 0.28F, 1.0F);
    const float thunderRumbleSeconds = std::clamp(1.8F + strikeDistanceMeters / 2600.0F, 2.0F, 5.5F);

    return ThunderstormPresentationSample{
        .hasStrike = true,
        .strikeIndex = latestStrikeIndex,
        .strikeStartPresentationTimeSeconds = latestStrikeStart,
        .thunderDuePresentationTimeSeconds = thunderDue,
        .flashIntensity = ThunderstormPresentationDetail::FlashPulse(
            presentationTimeSeconds - latestStrikeStart),
        .lightningViewportX = viewportX,
        .lightningPatternOffset = patternOffset,
        .strikeDistanceMeters = strikeDistanceMeters,
        .thunderGain = thunderGain,
        .thunderRumbleSeconds = thunderRumbleSeconds};
}

// Presentation-only cue tracker. It observes the deterministic strike schedule and turns the physical
// light-to-sound delay into semantic one-shot cues. No miniaudio/backend type or gameplay hazard state enters here.
class ThunderstormCueTracker final
{
public:
    void Reset() noexcept
    {
        initialized_ = false;
        previousPresentationTimeSeconds_ = 0.0;
        lastObservedStrikeIndex_.reset();
        pending_.clear();
    }

    [[nodiscard]] std::vector<ThunderAudioCue> Advance(
        const Environment::WeatherState& weather,
        const double presentationTimeSeconds)
    {
        std::vector<ThunderAudioCue> ready;
        if (!std::isfinite(presentationTimeSeconds) || presentationTimeSeconds < 0.0)
        {
            Reset();
            return ready;
        }
        if (!initialized_ || presentationTimeSeconds < previousPresentationTimeSeconds_)
        {
            initialized_ = true;
            previousPresentationTimeSeconds_ = presentationTimeSeconds;
            lastObservedStrikeIndex_.reset();
            pending_.clear();
            const auto current = EvaluateThunderstormPresentation(weather, presentationTimeSeconds);
            if (current.hasStrike)
                lastObservedStrikeIndex_ = current.strikeIndex;
            return ready;
        }

        const auto current = EvaluateThunderstormPresentation(weather, presentationTimeSeconds);
        if (current.hasStrike &&
            (!lastObservedStrikeIndex_.has_value() || current.strikeIndex != *lastObservedStrikeIndex_))
        {
            lastObservedStrikeIndex_ = current.strikeIndex;
            pending_.push_back(PendingCue{
                .strikeIndex = current.strikeIndex,
                .duePresentationTimeSeconds = current.thunderDuePresentationTimeSeconds,
                .gain = current.thunderGain,
                .distanceMeters = current.strikeDistanceMeters,
                .rumbleSeconds = current.thunderRumbleSeconds});
        }

        for (auto iterator = pending_.begin(); iterator != pending_.end();)
        {
            if (iterator->duePresentationTimeSeconds <= presentationTimeSeconds)
            {
                ready.push_back(ThunderAudioCue{
                    .strikeIndex = iterator->strikeIndex,
                    .gain = iterator->gain,
                    .distanceMeters = iterator->distanceMeters,
                    .rumbleSeconds = iterator->rumbleSeconds});
                iterator = pending_.erase(iterator);
            }
            else
            {
                ++iterator;
            }
        }
        previousPresentationTimeSeconds_ = presentationTimeSeconds;
        return ready;
    }

private:
    struct PendingCue final
    {
        std::uint64_t strikeIndex = 0U;
        double duePresentationTimeSeconds = 0.0;
        float gain = 0.0F;
        float distanceMeters = 0.0F;
        float rumbleSeconds = 0.0F;
    };

    bool initialized_ = false;
    double previousPresentationTimeSeconds_ = 0.0;
    std::optional<std::uint64_t> lastObservedStrikeIndex_{};
    std::vector<PendingCue> pending_{};
};
} // namespace DeepRun::Game
