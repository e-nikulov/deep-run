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
// Local torpedo-seeker quality gate. Unlike launch targeting, a local seeker is allowed to steer from a
// bearing-only perceived track; it still cannot consume source entity identity or authoritative position.
struct TorpedoSeekerConfig final
{
    float minimumTrackConfidence = 0.35F;
    float maximumBearingUncertaintyRadians = 0.20F;
    bool allowCoastingTrack = false;
};

enum class TorpedoSeekerMode : std::uint8_t
{
    Dormant,
    PassiveSearch,
    PassiveTrack,
    ActiveSearch,
    ActiveTrack,
    Reacquire,
    Exhausted,
};

enum class TorpedoSeekerMissReason : std::uint8_t
{
    None,
    NoInitialAcquisition,
    ContactLost,
};

// GAME POLICY only. These timings express an understandable mixed active/passive seeker state machine; they are
// not exact operational parameters for any real torpedo or sonar head.
struct TorpedoSeekerModeConfig final
{
    double passiveSearchBeforeActiveSeconds = 1.5;
    double activePingIntervalSeconds = 1.0;
    double lostContactBeforeActiveSeconds = 0.35;
    double maximumSearchWithoutContactSeconds = 30.0;
    bool preferPassiveCue = true;
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
    TorpedoSeekerMode mode = TorpedoSeekerMode::Dormant;
    TorpedoSeekerMissReason missReason = TorpedoSeekerMissReason::None;
    std::optional<double> activationTimeSeconds{};
    std::optional<double> lastContactTimeSeconds{};
    double nextActivePingTimeSeconds = 0.0;
    std::uint32_t activePingCount = 0U;
};

struct TorpedoSeekerModeDecision final
{
    TorpedoSeekerMode mode = TorpedoSeekerMode::Dormant;
    std::optional<TorpedoSeekerCue> guidanceCue{};
    bool requestActivePing = false;
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

[[nodiscard]] inline std::expected<void, std::string> ValidateTorpedoSeekerModeConfig(
    const TorpedoSeekerModeConfig& config)
{
    if (!std::isfinite(config.passiveSearchBeforeActiveSeconds) || config.passiveSearchBeforeActiveSeconds < 0.0 ||
        !std::isfinite(config.activePingIntervalSeconds) || config.activePingIntervalSeconds <= 0.0 ||
        !std::isfinite(config.lostContactBeforeActiveSeconds) || config.lostContactBeforeActiveSeconds < 0.0 ||
        !std::isfinite(config.maximumSearchWithoutContactSeconds) || config.maximumSearchWithoutContactSeconds <= 0.0 ||
        config.maximumSearchWithoutContactSeconds < config.passiveSearchBeforeActiveSeconds)
    {
        return std::unexpected("torpedo seeker mode configuration is invalid");
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

[[nodiscard]] inline bool IsValidTorpedoSeekerCue(
    const TorpedoSeekerConfig& config,
    const TorpedoSeekerCue& cue) noexcept
{
    return cue.seekerTrackId != 0U && std::isfinite(cue.bearingRadians) &&
        std::isfinite(cue.bearingUncertaintyRadians) && cue.bearingUncertaintyRadians >= 0.0F &&
        cue.bearingUncertaintyRadians <= config.maximumBearingUncertaintyRadians &&
        std::isfinite(cue.confidence) && cue.confidence >= config.minimumTrackConfidence && cue.confidence <= 1.0F;
}

// Pure perceived-world selector used by the mixed-mode state machine. Confidence wins first, then smaller
// angular uncertainty, then stable track ID. No classification/source/entity truth is consulted; a convincing
// countermeasure can therefore win exactly like a real source.
[[nodiscard]] inline std::expected<std::optional<TorpedoSeekerCue>, std::string> SelectBestTorpedoSeekerCue(
    const TorpedoSeekerConfig& config,
    const std::vector<Perception::Track>& perceivedTracks)
{
    const auto valid = ValidateTorpedoSeekerConfig(config);
    if (!valid)
    {
        return std::unexpected(valid.error());
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

    if (best == nullptr)
    {
        return std::optional<TorpedoSeekerCue>{};
    }
    return std::optional<TorpedoSeekerCue>{TorpedoSeekerCue{
        .seekerTrackId = best->trackId,
        .bearingRadians = best->estimatedBearingRadians,
        .bearingUncertaintyRadians = best->bearingUncertaintyRadians,
        .confidence = best->confidence}};
}

// Deterministic perceived-world selection retained for the original passive-only call sites and tests.
[[nodiscard]] inline std::expected<std::optional<TorpedoSeekerCue>, std::string> SelectTorpedoSeekerCue(
    const TorpedoSeekerConfig& config,
    TorpedoSeekerRuntimeState& state,
    const std::vector<Perception::Track>& perceivedTracks,
    const double simulationTimeSeconds)
{
    if (!std::isfinite(simulationTimeSeconds) || simulationTimeSeconds < state.lastUpdateTimeSeconds)
    {
        return std::unexpected("torpedo seeker time must be finite and monotonic");
    }
    const auto selected = SelectBestTorpedoSeekerCue(config, perceivedTracks);
    if (!selected)
    {
        return std::unexpected(selected.error());
    }

    state.lastUpdateTimeSeconds = simulationTimeSeconds;
    if (!selected->has_value())
    {
        state.selectedTrackId = std::nullopt;
        return std::optional<TorpedoSeekerCue>{};
    }
    state.selectedTrackId = (*selected)->seekerTrackId;
    return *selected;
}

// Mixed active/passive local seeker policy. `seekerEnabled` is normally false during launch-clearance/straight
// run. Once enabled, a good passive cue is preferred because it does not require transmission. With no passive
// cue the seeker requests active pinging after a bounded passive-search interval; after losing an acquired cue it
// enters reacquisition and can request active pings sooner. This state machine consumes only local perceived cues.
[[nodiscard]] inline std::expected<TorpedoSeekerModeDecision, std::string> UpdateTorpedoSeekerMode(
    const TorpedoSeekerConfig& seekerConfig,
    const TorpedoSeekerModeConfig& modeConfig,
    TorpedoSeekerRuntimeState& state,
    const bool seekerEnabled,
    const std::optional<TorpedoSeekerCue>& passiveCue,
    const std::optional<TorpedoSeekerCue>& activeCue,
    const double simulationTimeSeconds)
{
    const auto seekerValid = ValidateTorpedoSeekerConfig(seekerConfig);
    const auto modeValid = ValidateTorpedoSeekerModeConfig(modeConfig);
    if (!seekerValid)
    {
        return std::unexpected(seekerValid.error());
    }
    if (!modeValid)
    {
        return std::unexpected(modeValid.error());
    }
    if (!std::isfinite(simulationTimeSeconds) || simulationTimeSeconds < state.lastUpdateTimeSeconds ||
        (passiveCue && !IsValidTorpedoSeekerCue(seekerConfig, *passiveCue)) ||
        (activeCue && !IsValidTorpedoSeekerCue(seekerConfig, *activeCue)))
    {
        return std::unexpected("torpedo seeker mode input is invalid or time-reversing");
    }

    if (!seekerEnabled)
    {
        state.selectedTrackId = std::nullopt;
        state.mode = TorpedoSeekerMode::Dormant;
        state.missReason = TorpedoSeekerMissReason::None;
        state.activationTimeSeconds.reset();
        state.lastContactTimeSeconds.reset();
        state.nextActivePingTimeSeconds = simulationTimeSeconds;
        state.lastUpdateTimeSeconds = simulationTimeSeconds;
        return TorpedoSeekerModeDecision{.mode = state.mode};
    }

    if (!state.activationTimeSeconds)
    {
        state.activationTimeSeconds = simulationTimeSeconds;
        state.nextActivePingTimeSeconds = simulationTimeSeconds + modeConfig.passiveSearchBeforeActiveSeconds;
        state.mode = TorpedoSeekerMode::PassiveSearch;
        state.missReason = TorpedoSeekerMissReason::None;
    }

    const bool choosePassive = passiveCue.has_value() && (modeConfig.preferPassiveCue || !activeCue.has_value());
    const std::optional<TorpedoSeekerCue> chosen = choosePassive ? passiveCue : (activeCue ? activeCue : passiveCue);
    if (chosen)
    {
        state.selectedTrackId = chosen->seekerTrackId;
        state.lastContactTimeSeconds = simulationTimeSeconds;
        state.mode = choosePassive ? TorpedoSeekerMode::PassiveTrack : TorpedoSeekerMode::ActiveTrack;
        state.missReason = TorpedoSeekerMissReason::None;
        state.lastUpdateTimeSeconds = simulationTimeSeconds;
        return TorpedoSeekerModeDecision{
            .mode = state.mode,
            .guidanceCue = chosen,
            .requestActivePing = false};
    }

    state.selectedTrackId = std::nullopt;
    const double activeElapsed = simulationTimeSeconds - *state.activationTimeSeconds;
    const bool hadContact = state.lastContactTimeSeconds.has_value();
    const double lostForSeconds = hadContact
        ? simulationTimeSeconds - *state.lastContactTimeSeconds
        : activeElapsed;

    if (lostForSeconds >= modeConfig.maximumSearchWithoutContactSeconds)
    {
        state.mode = TorpedoSeekerMode::Exhausted;
        state.missReason = hadContact
            ? TorpedoSeekerMissReason::ContactLost
            : TorpedoSeekerMissReason::NoInitialAcquisition;
        state.lastUpdateTimeSeconds = simulationTimeSeconds;
        return TorpedoSeekerModeDecision{.mode = state.mode};
    }

    bool requestActivePing = false;
    if (!hadContact && activeElapsed < modeConfig.passiveSearchBeforeActiveSeconds)
    {
        state.mode = TorpedoSeekerMode::PassiveSearch;
    }
    else if (hadContact)
    {
        state.mode = TorpedoSeekerMode::Reacquire;
        state.missReason = TorpedoSeekerMissReason::ContactLost;
        if (lostForSeconds >= modeConfig.lostContactBeforeActiveSeconds &&
            simulationTimeSeconds + 1.0e-9 >= state.nextActivePingTimeSeconds)
        {
            requestActivePing = true;
        }
    }
    else
    {
        state.mode = TorpedoSeekerMode::ActiveSearch;
        state.missReason = TorpedoSeekerMissReason::NoInitialAcquisition;
        if (simulationTimeSeconds + 1.0e-9 >= state.nextActivePingTimeSeconds)
        {
            requestActivePing = true;
        }
    }

    state.lastUpdateTimeSeconds = simulationTimeSeconds;
    return TorpedoSeekerModeDecision{
        .mode = state.mode,
        .guidanceCue = std::nullopt,
        .requestActivePing = requestActivePing};
}

[[nodiscard]] inline std::expected<void, std::string> NotifyTorpedoSeekerActivePingEmitted(
    const TorpedoSeekerModeConfig& modeConfig,
    TorpedoSeekerRuntimeState& state,
    const double simulationTimeSeconds)
{
    const auto valid = ValidateTorpedoSeekerModeConfig(modeConfig);
    if (!valid)
    {
        return std::unexpected(valid.error());
    }
    if (!std::isfinite(simulationTimeSeconds) || simulationTimeSeconds < state.lastUpdateTimeSeconds ||
        state.mode == TorpedoSeekerMode::Dormant || state.mode == TorpedoSeekerMode::Exhausted)
    {
        return std::unexpected("torpedo seeker active-ping notification is invalid");
    }
    state.nextActivePingTimeSeconds = simulationTimeSeconds + modeConfig.activePingIntervalSeconds;
    ++state.activePingCount;
    return {};
}

// Seeker-mode movement uses the same authored speed/turn-rate limits as ordinary torpedo guidance but consumes
// only a bearing cue selected from local perceived tracks. It deliberately leaves the launch guidanceTrackId
// untouched: seeker-local track identity and launch/fire-control track identity are separate knowledge domains.
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
    const auto expired = ExpireConventionalTorpedoEnduranceIfNeeded(definition, torpedo, simulationTimeSeconds);
    if (!expired)
    {
        return std::unexpected(expired.error());
    }
    if (*expired)
    {
        return {};
    }
    if (torpedo.weapon.definitionId != definition.weapon.id || torpedo.weapon.phase != WeaponPhase::Launched ||
        torpedo.movementDomain != MovementDomain::Underwater || torpedo.impactedBody.has_value() ||
        !torpedo.positionMeters.IsFinite() || !std::isfinite(torpedo.headingRadians) ||
        !std::isfinite(torpedo.speedMetersPerSecond) || torpedo.speedMetersPerSecond <= 0.0F ||
        !IsValidTorpedoSeekerCue(seekerConfig, cue) ||
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
    torpedo.headingRadians = ClampConventionalTorpedoVerticalCourse(
        torpedo.headingRadians, definition.maximumVerticalCourseAngleRadians);

    const float distanceMeters = torpedo.speedMetersPerSecond * static_cast<float>(deltaSeconds);
    torpedo.positionMeters.x += static_cast<float>(std::cos(static_cast<double>(torpedo.headingRadians))) * distanceMeters;
    torpedo.positionMeters.y += static_cast<float>(std::sin(static_cast<double>(torpedo.headingRadians))) * distanceMeters;
    torpedo.lastUpdateTimeSeconds = simulationTimeSeconds;
    torpedo.weapon.lastUpdateTimeSeconds = simulationTimeSeconds;
    return {};
}

// Seeker-local bearing guidance still uses the same authoritative swept collision contract as ordinary torpedo
// guidance. The seeker never receives a target body; a PhysicsWorld hit is the first point where body identity
// can enter terminal weapon state.
[[nodiscard]] inline std::expected<std::optional<ConventionalTorpedoImpact>, std::string>
AdvanceConventionalTorpedoWithSeekerCueAndCollision(
    const ConventionalTorpedoDefinition& definition,
    const TorpedoSeekerConfig& seekerConfig,
    ConventionalTorpedoRuntimeState& torpedo,
    const TorpedoSeekerCue& cue,
    Physics::PhysicsWorld& physicsWorld,
    const double simulationTimeSeconds,
    const Physics::PhysicsBodyHandle ignoredBody = {})
{
    if (torpedo.movementDomain != MovementDomain::Underwater || torpedo.impactedBody.has_value())
    {
        return std::unexpected("spent/non-underwater conventional torpedo cannot advance with seeker collision");
    }

    const Physics::PhysicsVector3 startPosition = torpedo.positionMeters;
    ConventionalTorpedoRuntimeState candidate = torpedo;
    const auto movement = AdvanceConventionalTorpedoWithSeekerCue(
        definition, seekerConfig, candidate, cue, simulationTimeSeconds);
    if (!movement)
    {
        return std::unexpected(movement.error());
    }

    const Physics::PhysicsVector3 displacement{
        .x = candidate.positionMeters.x - startPosition.x,
        .y = candidate.positionMeters.y - startPosition.y,
        .z = candidate.positionMeters.z - startPosition.z};
    if (displacement.x == 0.0F && displacement.y == 0.0F && displacement.z == 0.0F)
    {
        torpedo = candidate;
        return std::optional<ConventionalTorpedoImpact>{};
    }

    const auto sweep = physicsWorld.SweepBoxClosest(Physics::PhysicsBoxSweepQuery{
        .halfExtentsMeters = definition.collisionHalfExtentsMeters,
        .startPositionMeters = startPosition,
        .orientation = WeaponHeadingQuaternion(candidate.headingRadians),
        .displacementMeters = displacement,
        .ignoredBody = ignoredBody});
    if (!sweep)
    {
        return std::unexpected("torpedo seeker physics sweep failed: " + sweep.error().message);
    }
    if (!*sweep)
    {
        torpedo = candidate;
        return std::optional<ConventionalTorpedoImpact>{};
    }

    const Physics::PhysicsSweepHit hit = **sweep;
    candidate.positionMeters = hit.positionMeters;
    candidate.speedMetersPerSecond = 0.0F;
    candidate.movementDomain = MovementDomain::Spent;
    candidate.terminalReason = ConventionalTorpedoTerminalReason::Impact;
    candidate.impactedBody = hit.body;
    candidate.lastUpdateTimeSeconds = simulationTimeSeconds;
    candidate.weapon.lastUpdateTimeSeconds = simulationTimeSeconds;
    torpedo = candidate;

    return std::optional<ConventionalTorpedoImpact>{ConventionalTorpedoImpact{
        .physicsHit = hit,
        .damage = Combat::CombatDamageEvent{
            .targetBody = hit.body,
            .positionMeters = hit.positionMeters,
            .damage = definition.directImpactDamage,
            .simulationTimeSeconds = simulationTimeSeconds},
        .explosion = Combat::CombatExplosionEvent{
            .positionMeters = hit.positionMeters,
            .nominalDamage = definition.directImpactDamage,
            .radiusMeters = definition.explosionRadiusMeters,
            .simulationTimeSeconds = simulationTimeSeconds}}};
}
} // namespace DeepRun::Weapons
