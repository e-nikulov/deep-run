#pragma once

#include "Simulation/Weapons/WeaponRuntime.h"

#include <cmath>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <vector>

namespace DeepRun::Game::Combat
{
// M5-F bounded enemy-combat behavior. This is deliberately a game-specific controller rather than a generic
// AI framework. It sees only perceived Tracks and authoritative own weapon state; no hostile entity/body/
// Transform is accepted by this API.
struct SimpleDestroyerCombatConfig final
{
    float minimumAwarenessConfidence = 0.35F;
    float maximumAwarenessBearingUncertaintyRadians = 0.35F;
    bool allowCoastingAwareness = false;
};

enum class SimpleDestroyerCombatAction
{
    Hold,
    PrepareWeapon,
    TrackTarget,
    LaunchWeapon,
};

struct SimpleDestroyerCombatDecision final
{
    SimpleDestroyerCombatAction action = SimpleDestroyerCombatAction::Hold;
    std::optional<std::uint64_t> perceivedTrackId{};
};

struct SimpleDestroyerCombatState final
{
    std::optional<std::uint64_t> selectedTrackId{};
    double lastUpdateTimeSeconds = 0.0;
};

[[nodiscard]] inline std::expected<void, std::string> ValidateSimpleDestroyerCombatConfig(
    const SimpleDestroyerCombatConfig& config)
{
    if (!std::isfinite(config.minimumAwarenessConfidence) || config.minimumAwarenessConfidence < 0.0F ||
        config.minimumAwarenessConfidence > 1.0F ||
        !std::isfinite(config.maximumAwarenessBearingUncertaintyRadians) ||
        config.maximumAwarenessBearingUncertaintyRadians < 0.0F ||
        config.maximumAwarenessBearingUncertaintyRadians > 3.1415927F)
    {
        return std::unexpected("simple destroyer combat configuration is invalid");
    }
    return {};
}

[[nodiscard]] inline bool IsTrackVisibleToSimpleDestroyer(
    const SimpleDestroyerCombatConfig& config,
    const Perception::Track& track) noexcept
{
    const bool lifecycleAccepted = track.lifecycle == Perception::TrackLifecycleState::Confirmed ||
        (config.allowCoastingAwareness && track.lifecycle == Perception::TrackLifecycleState::Coasting);
    return track.trackId != 0U && lifecycleAccepted && std::isfinite(track.confidence) &&
        track.confidence >= config.minimumAwarenessConfidence && track.confidence <= 1.0F &&
        std::isfinite(track.estimatedBearingRadians) && std::isfinite(track.bearingUncertaintyRadians) &&
        track.bearingUncertaintyRadians >= 0.0F &&
        track.bearingUncertaintyRadians <= config.maximumAwarenessBearingUncertaintyRadians;
}

// Selectors return perceived Track values rather than pointers into caller-owned containers. This keeps the
// helper safe when a caller constructs a temporary candidate list and avoids configuration-dependent dangling
// pointer behavior while preserving the no-ground-truth boundary.
[[nodiscard]] inline std::optional<Perception::Track> SelectBestSimpleDestroyerTrack(
    const SimpleDestroyerCombatConfig& config,
    const std::vector<Perception::Track>& tracks)
{
    std::optional<Perception::Track> best{};
    for (const auto& track : tracks)
    {
        if (!IsTrackVisibleToSimpleDestroyer(config, track))
        {
            continue;
        }
        if (!best || track.confidence > best->confidence ||
            (track.confidence == best->confidence && track.bearingUncertaintyRadians < best->bearingUncertaintyRadians) ||
            (track.confidence == best->confidence && track.bearingUncertaintyRadians == best->bearingUncertaintyRadians &&
             track.trackId < best->trackId))
        {
            best = track;
        }
    }
    return best;
}

[[nodiscard]] inline std::optional<Perception::Track> SelectBestWeaponQualifiedTrack(
    const Weapons::WeaponDefinition& weaponDefinition,
    const SimpleDestroyerCombatConfig& config,
    const std::vector<Perception::Track>& tracks)
{
    std::optional<Perception::Track> best{};
    for (const auto& track : tracks)
    {
        if (!IsTrackVisibleToSimpleDestroyer(config, track) ||
            !Weapons::ValidateTrackForWeapon(weaponDefinition, track))
        {
            continue;
        }
        if (!best || track.confidence > best->confidence ||
            (track.confidence == best->confidence && track.bearingUncertaintyRadians < best->bearingUncertaintyRadians) ||
            (track.confidence == best->confidence && track.bearingUncertaintyRadians == best->bearingUncertaintyRadians &&
             track.trackId < best->trackId))
        {
            best = track;
        }
    }
    return best;
}

// One deterministic fixed-step combat decision. Preparation may begin from weaker awareness evidence, but launch
// requires a currently present weapon-qualified Track on this exact tick. A stale targetTrackId retained in the
// weapon runtime is never sufficient by itself.
[[nodiscard]] inline std::expected<SimpleDestroyerCombatDecision, std::string> AdvanceSimpleDestroyerCombat(
    const SimpleDestroyerCombatConfig& config,
    const Weapons::WeaponDefinition& weaponDefinition,
    SimpleDestroyerCombatState& controller,
    Weapons::WeaponRuntimeState& weapon,
    const std::vector<Perception::Track>& perceivedTracks,
    const double simulationTimeSeconds)
{
    const auto configValid = ValidateSimpleDestroyerCombatConfig(config);
    const auto weaponDefinitionValid = Weapons::ValidateWeaponDefinition(weaponDefinition);
    if (!configValid)
    {
        return std::unexpected(configValid.error());
    }
    if (!weaponDefinitionValid)
    {
        return std::unexpected(weaponDefinitionValid.error());
    }
    if (weapon.definitionId != weaponDefinition.id || !std::isfinite(simulationTimeSeconds) ||
        simulationTimeSeconds < controller.lastUpdateTimeSeconds || simulationTimeSeconds < weapon.lastUpdateTimeSeconds)
    {
        return std::unexpected("simple destroyer combat state is invalid or time-reversing");
    }

    const auto awarenessTrack = SelectBestSimpleDestroyerTrack(config, perceivedTracks);
    const auto qualifiedTrack = SelectBestWeaponQualifiedTrack(weaponDefinition, config, perceivedTracks);

    if (weapon.phase == Weapons::WeaponPhase::Launched)
    {
        controller.selectedTrackId = weapon.targetTrackId;
        controller.lastUpdateTimeSeconds = simulationTimeSeconds;
        const auto advanced = Weapons::AdvanceWeaponReadiness(weaponDefinition, weapon, simulationTimeSeconds);
        if (!advanced)
        {
            return std::unexpected(advanced.error());
        }
        return SimpleDestroyerCombatDecision{
            .action = SimpleDestroyerCombatAction::Hold,
            .perceivedTrackId = controller.selectedTrackId};
    }

    if (weapon.phase == Weapons::WeaponPhase::Stored)
    {
        if (!awarenessTrack)
        {
            const auto advanced = Weapons::AdvanceWeaponReadiness(weaponDefinition, weapon, simulationTimeSeconds);
            if (!advanced)
            {
                return std::unexpected(advanced.error());
            }
            controller.selectedTrackId = std::nullopt;
            controller.lastUpdateTimeSeconds = simulationTimeSeconds;
            return SimpleDestroyerCombatDecision{};
        }

        const auto prepared = Weapons::PrepareWeapon(weaponDefinition, weapon, simulationTimeSeconds);
        if (!prepared)
        {
            return std::unexpected(prepared.error());
        }
        controller.selectedTrackId = awarenessTrack->trackId;

        if (qualifiedTrack)
        {
            const auto assigned = Weapons::AssignWeaponTarget(
                weaponDefinition, weapon, *qualifiedTrack, simulationTimeSeconds);
            if (!assigned)
            {
                return std::unexpected(assigned.error());
            }
            controller.selectedTrackId = qualifiedTrack->trackId;
        }

        controller.lastUpdateTimeSeconds = simulationTimeSeconds;
        return SimpleDestroyerCombatDecision{
            .action = SimpleDestroyerCombatAction::PrepareWeapon,
            .perceivedTrackId = controller.selectedTrackId};
    }

    const auto advanced = Weapons::AdvanceWeaponReadiness(weaponDefinition, weapon, simulationTimeSeconds);
    if (!advanced)
    {
        return std::unexpected(advanced.error());
    }

    if (!qualifiedTrack)
    {
        controller.selectedTrackId = awarenessTrack
            ? std::optional<std::uint64_t>{awarenessTrack->trackId}
            : std::nullopt;
        controller.lastUpdateTimeSeconds = simulationTimeSeconds;
        return SimpleDestroyerCombatDecision{
            .action = SimpleDestroyerCombatAction::Hold,
            .perceivedTrackId = controller.selectedTrackId};
    }

    const auto assigned = Weapons::AssignWeaponTarget(
        weaponDefinition, weapon, *qualifiedTrack, simulationTimeSeconds);
    if (!assigned)
    {
        return std::unexpected(assigned.error());
    }
    controller.selectedTrackId = qualifiedTrack->trackId;

    if (weapon.phase == Weapons::WeaponPhase::Ready)
    {
        const auto launched = Weapons::LaunchWeapon(weaponDefinition, weapon, simulationTimeSeconds);
        if (!launched)
        {
            return std::unexpected(launched.error());
        }
        controller.lastUpdateTimeSeconds = simulationTimeSeconds;
        return SimpleDestroyerCombatDecision{
            .action = SimpleDestroyerCombatAction::LaunchWeapon,
            .perceivedTrackId = qualifiedTrack->trackId};
    }

    controller.lastUpdateTimeSeconds = simulationTimeSeconds;
    return SimpleDestroyerCombatDecision{
        .action = SimpleDestroyerCombatAction::TrackTarget,
        .perceivedTrackId = qualifiedTrack->trackId};
}
} // namespace DeepRun::Game::Combat
