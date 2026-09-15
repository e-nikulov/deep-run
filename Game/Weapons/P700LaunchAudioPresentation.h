#pragma once

#include "Engine/Audio/ProceduralOneShot.h"
#include "Game/Weapons/P700LaunchVfx.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>

namespace DeepRun::Game::Armament
{
namespace P700LaunchAudioDetail
{
struct Profile final
{
    float gain = 0.5F;
    float durationSeconds = 1.0F;
    float attackSeconds = 0.01F;
    float releaseSeconds = 0.7F;
    float lowPassCutoffHz = 800.0F;
    float toneFrequencyHz = 60.0F;
    float toneMix = 0.2F;
    float transientMix = 0.2F;
    float referenceDistanceMeters = 150.0F;
    float maximumDistanceMeters = 2'000.0F;
};

[[nodiscard]] inline Profile ProfileFor(const P700LaunchAudioEvent event) noexcept
{
    switch (event)
    {
    case P700LaunchAudioEvent::LauncherOpen:
        return {.gain = 0.30F, .durationSeconds = 0.42F, .attackSeconds = 0.004F, .releaseSeconds = 0.30F,
                .lowPassCutoffHz = 1'200.0F, .toneFrequencyHz = 90.0F, .toneMix = 0.12F,
                .transientMix = 0.42F, .referenceDistanceMeters = 75.0F, .maximumDistanceMeters = 800.0F};
    case P700LaunchAudioEvent::UnderwaterIgnition:
        return {.gain = 0.82F, .durationSeconds = 1.80F, .attackSeconds = 0.008F, .releaseSeconds = 1.30F,
                .lowPassCutoffHz = 190.0F, .toneFrequencyHz = 42.0F, .toneMix = 0.35F,
                .transientMix = 0.16F, .referenceDistanceMeters = 180.0F, .maximumDistanceMeters = 1'800.0F};
    case P700LaunchAudioEvent::UnderwaterPass:
        return {.gain = 0.48F, .durationSeconds = 1.20F, .attackSeconds = 0.020F, .releaseSeconds = 0.90F,
                .lowPassCutoffHz = 150.0F, .toneFrequencyHz = 55.0F, .toneMix = 0.28F,
                .transientMix = 0.06F, .referenceDistanceMeters = 220.0F, .maximumDistanceMeters = 1'400.0F};
    case P700LaunchAudioEvent::SurfaceBreach:
        return {.gain = 0.92F, .durationSeconds = 1.10F, .attackSeconds = 0.003F, .releaseSeconds = 0.80F,
                .lowPassCutoffHz = 2'200.0F, .toneFrequencyHz = 58.0F, .toneMix = 0.08F,
                .transientMix = 0.58F, .referenceDistanceMeters = 220.0F, .maximumDistanceMeters = 2'600.0F};
    case P700LaunchAudioEvent::BoosterAir:
        return {.gain = 0.88F, .durationSeconds = 1.50F, .attackSeconds = 0.006F, .releaseSeconds = 1.10F,
                .lowPassCutoffHz = 900.0F, .toneFrequencyHz = 72.0F, .toneMix = 0.28F,
                .transientMix = 0.18F, .referenceDistanceMeters = 300.0F, .maximumDistanceMeters = 3'400.0F};
    case P700LaunchAudioEvent::WingDeploy:
        return {.gain = 0.35F, .durationSeconds = 0.35F, .attackSeconds = 0.002F, .releaseSeconds = 0.24F,
                .lowPassCutoffHz = 3'400.0F, .toneFrequencyHz = 180.0F, .toneMix = 0.10F,
                .transientMix = 0.55F, .referenceDistanceMeters = 120.0F, .maximumDistanceMeters = 1'200.0F};
    case P700LaunchAudioEvent::BoosterSeparate:
        return {.gain = 0.42F, .durationSeconds = 0.48F, .attackSeconds = 0.002F, .releaseSeconds = 0.32F,
                .lowPassCutoffHz = 2'400.0F, .toneFrequencyHz = 95.0F, .toneMix = 0.08F,
                .transientMix = 0.58F, .referenceDistanceMeters = 150.0F, .maximumDistanceMeters = 1'600.0F};
    case P700LaunchAudioEvent::TurbojetStart:
        return {.gain = 0.62F, .durationSeconds = 1.30F, .attackSeconds = 0.020F, .releaseSeconds = 1.00F,
                .lowPassCutoffHz = 820.0F, .toneFrequencyHz = 86.0F, .toneMix = 0.24F,
                .transientMix = 0.16F, .referenceDistanceMeters = 280.0F, .maximumDistanceMeters = 3'200.0F};
    case P700LaunchAudioEvent::Flyby:
        return {.gain = 0.70F, .durationSeconds = 0.85F, .attackSeconds = 0.010F, .releaseSeconds = 0.62F,
                .lowPassCutoffHz = 1'800.0F, .toneFrequencyHz = 110.0F, .toneMix = 0.07F,
                .transientMix = 0.40F, .referenceDistanceMeters = 420.0F, .maximumDistanceMeters = 4'500.0F};
    }
    return {};
}

[[nodiscard]] inline bool FinitePosition(const std::array<float, 3>& value) noexcept
{
    return std::isfinite(value[0]) && std::isfinite(value[1]) && std::isfinite(value[2]);
}

[[nodiscard]] inline float DistanceMeters(
    const std::array<float, 3>& first, const std::array<float, 3>& second) noexcept
{
    const double dx = static_cast<double>(second[0]) - first[0];
    const double dy = static_cast<double>(second[1]) - first[1];
    const double dz = static_cast<double>(second[2]) - first[2];
    return static_cast<float>(std::sqrt(dx * dx + dy * dy + dz * dz));
}
} // namespace P700LaunchAudioDetail

// Game-owned semantic mapping into the generic non-spatialized one-shot synthesizer. Distance is intentionally
// converted to gain here because the current Engine/Audio primitive has no world semantics. A later spatial
// backend can replace this adapter without changing weapon lifecycle authority.
[[nodiscard]] inline std::expected<std::optional<Audio::ProceduralNoiseOneShotRequest>, std::string>
BuildP700LaunchAudioOneShotRequest(
    const P700LaunchAudioHook& hook,
    const std::array<float, 3>& listenerWorldPositionMeters)
{
    if (!P700LaunchAudioDetail::FinitePosition(hook.worldPositionMeters) ||
        !P700LaunchAudioDetail::FinitePosition(listenerWorldPositionMeters))
        return std::unexpected("P-700 launch audio received a non-finite world position");

    const auto profile = P700LaunchAudioDetail::ProfileFor(hook.event);
    const float distanceMeters = P700LaunchAudioDetail::DistanceMeters(
        hook.worldPositionMeters, listenerWorldPositionMeters);
    if (!std::isfinite(distanceMeters))
        return std::unexpected("P-700 launch audio derived a non-finite listener distance");
    if (distanceMeters >= profile.maximumDistanceMeters)
        return std::optional<Audio::ProceduralNoiseOneShotRequest>{};

    const float normalizedDistance = distanceMeters / profile.referenceDistanceMeters;
    const float distanceGain = 1.0F / std::sqrt(1.0F + normalizedDistance * normalizedDistance);
    const float mediumGain = hook.underwater ? 0.88F : 1.0F;
    const float cutoffHz = hook.underwater
        ? (std::min)(profile.lowPassCutoffHz, 520.0F)
        : profile.lowPassCutoffHz;
    const std::uint64_t semanticSalt =
        (static_cast<std::uint64_t>(hook.event) + 1ULL) * 0x9E3779B97F4A7C15ULL;

    Audio::ProceduralNoiseOneShotRequest request{
        .seed = hook.launchSeed ^ semanticSalt,
        .gain = std::clamp(profile.gain * distanceGain * mediumGain, 0.0F, 1.0F),
        .durationSeconds = profile.durationSeconds,
        .attackSeconds = profile.attackSeconds,
        .releaseSeconds = profile.releaseSeconds,
        .lowPassCutoffHz = cutoffHz,
        .toneFrequencyHz = profile.toneFrequencyHz,
        .toneMix = profile.toneMix,
        .transientMix = profile.transientMix};
    if (const auto valid = Audio::ValidateProceduralNoiseOneShotRequest(request, 48'000U); !valid)
        return std::unexpected("P-700 launch audio mapping produced invalid synthesis controls: " + valid.error());
    return std::optional<Audio::ProceduralNoiseOneShotRequest>{request};
}
} // namespace DeepRun::Game::Armament
