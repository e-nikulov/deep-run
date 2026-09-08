#pragma once

#include "Simulation/Perception/TrackManager.h"
#include "Simulation/Weapons/ConventionalTorpedo.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <vector>

namespace DeepRun::Weapons
{
// Local torpedo-seeker quality gate. Unlike launch targeting, a passive seeker is allowed to steer from a
// bearing-only perceived track; it still cannot consume source entity identity or authoritative position.
struct TorpedoSeekerConfig final
{
    float minimumTrackConfidence = 0.35F;
    float maximumBearingUncertaintyRadians = 0.20F;
    bool allowCoastingTrack = false;
};

struct TorpedoSeekerCue final
{
    std::uint64_t seekerTrackId = 0;
    float bearingRadians = 0.0F;
    float bearingUncertaintyRadians = 0.0F;
    float confidence = 0.0F;
};

struct TorpedoSeekerRuntimeState final
{
    std::optional<std::uint64_t> selectedTrackId{};
    double lastUpdateTimeSeconds = 0.0;
};

[[nodiscard]] inline std::expected<void, std::string> ValidateTorpedoSeekerConfig(const TorpedoSeekerConfig& config)
{
    if (!std::isfinite(config.minimumTrackConfidence) || config.minimumTrackConfidence < 0.0F ||
        config.minimumTrackConfidence > 1.0F || !std::isfinite(config.maximumBearingUncertaintyRadians) ||
        config.maximumBearingUncertaintyRadians < 0.0F || config.maximumBearingUncertaintyRadians > 3.1415927F)
    {
        return std::unexpected("torpedo seeker configuration is invalid");
    }
    return {};
}

[[nodiscard]] inline bool IsTrackEligibleForTorpedoSeeker(
    const TorpedoSeekerConfig& config,
    const Perception::Track& track) noexcept
{
    const bool lifecycleAccepted = track.lifecycle == Perception::TrackLifecycleState::Confirmed ||
        (config.allowCoastingTrack && track.lifecycle == Perception::TrackLifecycleState::Coasting);
    return track.trackId != 0U && lifecycleAccepted && std::isfinite(track.estimatedBearingRadians) &&
        std::isfinite(track.bearingUncertaintyRadians) && track.bearingUncertaintyRadians >= 0.0F &&
        track.bearingUncertaintyRadians <= config.maximumBearingUncertaintyRadians &&
        std::isfinite(track.confidence) && track.confidence >= config.minimumTrackConfidence && track.confidence <= 1.0F;
}

// Deterministic perceived-world selection. Confidence wins first, then smaller angular uncertainty, then stable
// track ID. No classification/source/entity truth is consulted; a sufficiently convincing decoy can therefore
// win exactly the same way a real source can.
[[nodiscard]] inline std::expected<std::optional<TorpedoSeekerCue>, std::string> SelectTorpedoSeekerCue(
    const TorpedoSeekerConfig& config,
    TorpedoSeekerRuntimeState& state,
    const std::vector<Perception::Track>& perceivedTracks,
    const double simulationTimeSeconds)
{
    const auto valid = ValidateTorpedoSeekerConfig(config);
    if (!valid)
    {
        return std::unexpected(valid.error());
    }
    if (!std::isfinite(simulationTimeSeconds) || simulationTimeSeconds < state.lastUpdateTimeSeconds)
    {
        return std::unexpected("torpedo seeker time must be finite and monotonic");
    }

    const Perception::Track* best = nullptr;
    for (const auto& track : perceivedTracks)
    {
        if (!IsTrackEligibleForTorpedoSeeker(config, track))
        {
            continue;
        }
        if (best == nullptr || track.confidence > best->confidence ||
            (track.confidence == best->confidence && track.bearingUncertaintyRadians < best->bearingUncertaintyRadians) ||
            (track.confidence == best->confidence && track.bearingUncertaintyRadians == best->bearingUncertaintyRadians &&
             track.trackId < best->trackId))
        {
            best = &track;
        }
    }

    state.lastUpdateTimeSeconds = simulationTimeSeconds;
    if (best == nullptr)
    {
        state.selectedTrackId = std::nullopt;
        return std::optional<TorpedoSeekerCue>{};
    }

    state.selectedTrackId = best->trackId;
    return std::optional<TorpedoSeekerCue>{TorpedoSeekerCue{
        .seekerTrackId = best->trackId,
        .bearingRadians = best->estimatedBearingRadians,
        .bearingUncertaintyRadians = best->bearingUncertaintyRadians,
        .confidence = best->confidence}};
}

// Seeker-mode movement uses the same authored speed/turn-rate limits as ordinary torpedo guidance but consumes
// only a bearing cue selected from local perceived tracks. It deliberately leaves the launch guidanceTrackId
// untouched: seeker-local track identity and launch/datalink track identity are separate knowledge domains.
[[nodiscard]] inline std::expected<void, std::string> AdvanceConventionalTorpedoWithSeekerCue(
    const ConventionalTorpedoDefinition& definition,
    const TorpedoSeekerConfig& seekerConfig,
    ConventionalTorpedoRuntimeState& torpedo,
    const TorpedoSeekerCue& cue,
    const double simulationTimeSeconds)
{
    const auto definitionValid = ValidateConventionalTorpedoDefinition(definition);
    const auto seekerValid = ValidateTorpedoSeekerConfig(seekerConfig);
    if (!definitionValid)
    {
        return std::unexpected(definitionValid.error());
    }
    if (!seekerValid)
    {
        return std::unexpected(seekerValid.error());
    }
    if (torpedo.weapon.definitionId != definition.weapon.id || torpedo.weapon.phase != WeaponPhase::Launched ||
        torpedo.movementDomain != MovementDomain::Underwater || torpedo.impactedBody.has_value() ||
        !torpedo.positionMeters.IsFinite() || !std::isfinite(torpedo.headingRadians) ||
        !std::isfinite(torpedo.speedMetersPerSecond) || torpedo.speedMetersPerSecond <= 0.0F ||
        cue.seekerTrackId == 0U || !std::isfinite(cue.bearingRadians) ||
        !std::isfinite(cue.bearingUncertaintyRadians) || cue.bearingUncertaintyRadians < 0.0F ||
        cue.bearingUncertaintyRadians > seekerConfig.maximumBearingUncertaintyRadians ||
        !std::isfinite(cue.confidence) || cue.confidence < seekerConfig.minimumTrackConfidence || cue.confidence > 1.0F ||
        !std::isfinite(simulationTimeSeconds) || simulationTimeSeconds < torpedo.lastUpdateTimeSeconds ||
        simulationTimeSeconds < torpedo.weapon.lastUpdateTimeSeconds)
    {
        return std::unexpected("torpedo seeker guidance runtime/cue is invalid, weak, or time-reversing");
    }

    const double deltaSeconds = simulationTimeSeconds - torpedo.lastUpdateTimeSeconds;
    if (deltaSeconds == 0.0)
    {
        return {};
    }

    const float headingDelta = WrapWeaponHeading(cue.bearingRadians - torpedo.headingRadians);
    const float maximumTurn = definition.maximumTurnRateRadiansPerSecond * static_cast<float>(deltaSeconds);
    torpedo.headingRadians = WrapWeaponHeading(
        torpedo.headingRadians + std::clamp(headingDelta, -maximumTurn, maximumTurn));

    const float distanceMeters = torpedo.speedMetersPerSecond * static_cast<float>(deltaSeconds);
    torpedo.positionMeters.x += static_cast<float>(std::cos(static_cast<double>(torpedo.headingRadians))) * distanceMeters;
    torpedo.positionMeters.y += static_cast<float>(std::sin(static_cast<double>(torpedo.headingRadians))) * distanceMeters;
    torpedo.lastUpdateTimeSeconds = simulationTimeSeconds;
    torpedo.weapon.lastUpdateTimeSeconds = simulationTimeSeconds;
    return {};
}
} // namespace DeepRun::Weapons
