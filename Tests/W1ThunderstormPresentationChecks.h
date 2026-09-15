#pragma once

#include "Game/Environment/ThunderstormPresentation.h"

#include <cmath>
#include <cstddef>
#include <optional>

namespace DeepRun::Tests
{
[[nodiscard]] inline bool RunW1ThunderstormPresentationChecks()
{
    using namespace Environment;
    using namespace Game;

    const auto storm = WeatherState::Create(WeatherStateConfig{
        .beaufortForce = 9U,
        .windSpeedMetersPerSecond = 23.0F,
        .windGustSpeedMetersPerSecond = 30.0F,
        .windDirectionDegrees = 28.0F,
        .windSea = WindSeaState{
            .significantWaveHeightMeters = 7.0F,
            .probableMaximumWaveHeightMeters = 10.0F,
            .peakPeriodSeconds = 9.0F,
            .meanDirectionDegrees = 28.0F,
            .directionalSpreadDegrees = 34.0F},
        .swell = {},
        .rainRateMillimetersPerHour = 36.0F,
        .meteorologicalVisibilityMeters = 4500.0F,
        .cloudCoverFraction = 0.92F,
        .lightningRatePerMinute = 12.0F,
        .weatherSeed = 0x5731475F5448554EULL});
    if (!storm)
        return false;

    const auto calm = WeatherState::Create(WeatherStateConfig{
        .beaufortForce = 2U,
        .windSpeedMetersPerSecond = 3.0F,
        .windGustSpeedMetersPerSecond = 4.0F,
        .windDirectionDegrees = 0.0F,
        .windSea = WindSeaState{
            .significantWaveHeightMeters = 0.2F,
            .probableMaximumWaveHeightMeters = 0.3F,
            .peakPeriodSeconds = 2.3F,
            .meanDirectionDegrees = 0.0F,
            .directionalSpreadDegrees = 55.0F},
        .swell = {},
        .rainRateMillimetersPerHour = 0.0F,
        .meteorologicalVisibilityMeters = 100000.0F,
        .cloudCoverFraction = 0.05F,
        .lightningRatePerMinute = 0.0F,
        .weatherSeed = 17U});
    if (!calm)
        return false;

    const auto calmSample = EvaluateThunderstormPresentation(*calm, 120.0);
    if (calmSample.hasStrike || calmSample.flashIntensity != 0.0F || calmSample.thunderGain != 0.0F)
        return false;
    if (EvaluateThunderstormPresentation(*storm, -1.0).hasStrike)
        return false;

    // Discover one authored deterministic strike without depending on a hard-coded hash result.
    std::optional<ThunderstormPresentationSample> firstVisibleFlash;
    double firstVisibleFlashTime = 0.0;
    for (std::size_t step = 0U; step < 4000U; ++step)
    {
        const double timeSeconds = static_cast<double>(step) * 0.01;
        const auto sample = EvaluateThunderstormPresentation(*storm, timeSeconds);
        if (sample.hasStrike && sample.flashIntensity > 0.85F)
        {
            firstVisibleFlash = sample;
            firstVisibleFlashTime = timeSeconds;
            break;
        }
    }
    if (!firstVisibleFlash.has_value())
        return false;

    const auto repeated = EvaluateThunderstormPresentation(*storm, firstVisibleFlashTime);
    if (!repeated.hasStrike || repeated.strikeIndex != firstVisibleFlash->strikeIndex ||
        repeated.flashIntensity != firstVisibleFlash->flashIntensity ||
        repeated.lightningViewportX != firstVisibleFlash->lightningViewportX ||
        repeated.lightningPatternOffset != firstVisibleFlash->lightningPatternOffset ||
        repeated.strikeDistanceMeters != firstVisibleFlash->strikeDistanceMeters ||
        repeated.thunderDuePresentationTimeSeconds != firstVisibleFlash->thunderDuePresentationTimeSeconds)
    {
        return false;
    }

    if (!(firstVisibleFlash->lightningViewportX >= 0.12F && firstVisibleFlash->lightningViewportX <= 0.88F) ||
        !(firstVisibleFlash->lightningPatternOffset >= 0.0F && firstVisibleFlash->lightningPatternOffset <= 1.0F) ||
        !(firstVisibleFlash->strikeDistanceMeters >= 1200.0F && firstVisibleFlash->strikeDistanceMeters <= 9000.0F) ||
        !(firstVisibleFlash->thunderGain >= 0.28F && firstVisibleFlash->thunderGain <= 1.0F) ||
        !(firstVisibleFlash->thunderRumbleSeconds >= 2.0F && firstVisibleFlash->thunderRumbleSeconds <= 5.5F))
    {
        return false;
    }

    const double acousticDelay = firstVisibleFlash->thunderDuePresentationTimeSeconds -
        firstVisibleFlash->strikeStartPresentationTimeSeconds;
    const double expectedDelay = static_cast<double>(firstVisibleFlash->strikeDistanceMeters) /
        ThunderstormPresentationDetail::SpeedOfSoundMetersPerSecond;
    if (std::abs(acousticDelay - expectedDelay) > 1.0e-5)
        return false;

    // The optical flash must decay before its physically delayed thunder cue is due.
    const auto afterFlash = EvaluateThunderstormPresentation(
        *storm,
        firstVisibleFlash->strikeStartPresentationTimeSeconds +
            ThunderstormPresentationDetail::MaximumFlashAgeSeconds + 0.05);
    if (!afterFlash.hasStrike || afterFlash.strikeIndex != firstVisibleFlash->strikeIndex ||
        afterFlash.flashIntensity != 0.0F ||
        !(afterFlash.thunderDuePresentationTimeSeconds > firstVisibleFlash->strikeStartPresentationTimeSeconds))
    {
        return false;
    }

    ThunderstormCueTracker tracker;
    if (!tracker.Advance(*storm, 0.0).empty())
        return false;

    std::optional<ThunderAudioCue> firstCue;
    std::size_t cueCount = 0U;
    for (std::size_t step = 1U; step <= 4800U; ++step)
    {
        const double timeSeconds = static_cast<double>(step) * 0.05;
        const auto cues = tracker.Advance(*storm, timeSeconds);
        for (const auto& cue : cues)
        {
            ++cueCount;
            if (!firstCue.has_value())
                firstCue = cue;
            if (!(cue.gain >= 0.28F && cue.gain <= 1.0F) ||
                !(cue.distanceMeters >= 1200.0F && cue.distanceMeters <= 9000.0F) ||
                !(cue.rumbleSeconds >= 2.0F && cue.rumbleSeconds <= 5.5F))
            {
                return false;
            }
        }
    }
    if (!firstCue.has_value() || cueCount < 2U)
        return false;

    // Rewinding presentation time resets pending one-shots instead of replaying stale thunder.
    if (!tracker.Advance(*storm, 1.0).empty())
        return false;
    if (!tracker.Advance(*calm, 2.0).empty())
        return false;

    return true;
}
} // namespace DeepRun::Tests
