#pragma once

#include "Simulation/Perception/TrackManager.h"

#include <cmath>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <utility>

namespace DeepRun::Weapons
{
enum class WeaponPhase
{
    Stored,
    Preparing,
    Ready,
    Launched,
    Spent,
};

struct WeaponTargetingRequirements final
{
    float minimumTrackConfidence = 0.65F;
    float maximumBearingUncertaintyRadians = 0.17453293F; // 10 degrees; gameplay tuning, not sensor truth.
    bool requiresEstimatedPosition = true;
    bool allowCoastingTrack = false;
};

struct WeaponDefinition final
{
    std::string id{};
    double preparationSeconds = 5.0;
    WeaponTargetingRequirements targeting{};
};

// Authoritative weapon state for the bounded M5 combat slice. It keeps only perceived-world track identity;
// no target entity handle, Transform, or other ground-truth identity is stored here.
struct WeaponRuntimeState final
{
    std::string definitionId{};
    WeaponPhase phase = WeaponPhase::Stored;
    double phaseEnteredTimeSeconds = 0.0;
    double lastUpdateTimeSeconds = 0.0;
    std::optional<std::uint64_t> targetTrackId{};
};

[[nodiscard]] inline std::expected<void, std::string> ValidateWeaponDefinition(const WeaponDefinition& definition)
{
    if (definition.id.empty())
    {
        return std::unexpected("weapon definition id must not be empty");
    }
    if (!std::isfinite(definition.preparationSeconds) || definition.preparationSeconds < 0.0)
    {
        return std::unexpected("weapon preparation time must be finite and non-negative");
    }
    if (!std::isfinite(definition.targeting.minimumTrackConfidence) ||
        definition.targeting.minimumTrackConfidence < 0.0F || definition.targeting.minimumTrackConfidence > 1.0F)
    {
        return std::unexpected("weapon minimum track confidence must be within [0, 1]");
    }
    if (!std::isfinite(definition.targeting.maximumBearingUncertaintyRadians) ||
        definition.targeting.maximumBearingUncertaintyRadians < 0.0F)
    {
        return std::unexpected("weapon maximum bearing uncertainty must be finite and non-negative");
    }
    return {};
}

[[nodiscard]] inline std::expected<WeaponRuntimeState, std::string> CreateWeaponRuntime(
    const WeaponDefinition& definition, const double simulationTimeSeconds)
{
    const auto validDefinition = ValidateWeaponDefinition(definition);
    if (!validDefinition)
    {
        return std::unexpected(validDefinition.error());
    }
    if (!std::isfinite(simulationTimeSeconds) || simulationTimeSeconds < 0.0)
    {
        return std::unexpected("weapon simulation time must be finite and non-negative");
    }

    return WeaponRuntimeState{
        .definitionId = definition.id,
        .phase = WeaponPhase::Stored,
        .phaseEnteredTimeSeconds = simulationTimeSeconds,
        .lastUpdateTimeSeconds = simulationTimeSeconds,
        .targetTrackId = std::nullopt};
}

[[nodiscard]] inline std::expected<void, std::string> ValidateWeaponTime(
    const WeaponRuntimeState& state, const double simulationTimeSeconds)
{
    if (!std::isfinite(simulationTimeSeconds) || simulationTimeSeconds < state.lastUpdateTimeSeconds)
    {
        return std::unexpected("weapon simulation time must be finite and monotonic");
    }
    return {};
}

[[nodiscard]] inline std::expected<void, std::string> PrepareWeapon(
    const WeaponDefinition& definition, WeaponRuntimeState& state, const double simulationTimeSeconds)
{
    if (state.definitionId != definition.id)
    {
        return std::unexpected("weapon runtime does not match definition");
    }
    const auto validTime = ValidateWeaponTime(state, simulationTimeSeconds);
    if (!validTime)
    {
        return std::unexpected(validTime.error());
    }
    if (state.phase != WeaponPhase::Stored)
    {
        return std::unexpected("weapon can only be prepared from Stored phase");
    }

    state.phase = definition.preparationSeconds == 0.0 ? WeaponPhase::Ready : WeaponPhase::Preparing;
    state.phaseEnteredTimeSeconds = simulationTimeSeconds;
    state.lastUpdateTimeSeconds = simulationTimeSeconds;
    return {};
}

[[nodiscard]] inline std::expected<void, std::string> AdvanceWeaponReadiness(
    const WeaponDefinition& definition, WeaponRuntimeState& state, const double simulationTimeSeconds)
{
    if (state.definitionId != definition.id)
    {
        return std::unexpected("weapon runtime does not match definition");
    }
    const auto validTime = ValidateWeaponTime(state, simulationTimeSeconds);
    if (!validTime)
    {
        return std::unexpected(validTime.error());
    }

    state.lastUpdateTimeSeconds = simulationTimeSeconds;
    if (state.phase == WeaponPhase::Preparing &&
        simulationTimeSeconds - state.phaseEnteredTimeSeconds >= definition.preparationSeconds)
    {
        state.phase = WeaponPhase::Ready;
        state.phaseEnteredTimeSeconds = simulationTimeSeconds;
    }
    return {};
}

[[nodiscard]] inline std::expected<void, std::string> ValidateTrackForWeapon(
    const WeaponDefinition& definition, const Perception::Track& track)
{
    if (track.trackId == 0U)
    {
        return std::unexpected("weapon targeting requires a valid track id");
    }

    const bool lifecycleAccepted = track.lifecycle == Perception::TrackLifecycleState::Confirmed ||
        (definition.targeting.allowCoastingTrack && track.lifecycle == Perception::TrackLifecycleState::Coasting);
    if (!lifecycleAccepted)
    {
        return std::unexpected("weapon targeting requires an accepted track lifecycle");
    }
    if (!std::isfinite(track.confidence) || track.confidence < definition.targeting.minimumTrackConfidence)
    {
        return std::unexpected("weapon targeting requires sufficient track confidence");
    }
    if (!std::isfinite(track.bearingUncertaintyRadians) || track.bearingUncertaintyRadians < 0.0F ||
        track.bearingUncertaintyRadians > definition.targeting.maximumBearingUncertaintyRadians)
    {
        return std::unexpected("weapon targeting requires sufficiently bounded bearing uncertainty");
    }
    if (definition.targeting.requiresEstimatedPosition &&
        (!track.estimatedPositionMeters.has_value() || !track.estimatedPositionMeters->IsFinite()))
    {
        return std::unexpected("weapon targeting requires an estimated track position");
    }
    return {};
}

[[nodiscard]] inline std::expected<void, std::string> AssignWeaponTarget(
    const WeaponDefinition& definition, WeaponRuntimeState& state, const Perception::Track& track)
{
    if (state.definitionId != definition.id)
    {
        return std::unexpected("weapon runtime does not match definition");
    }
    if (state.phase != WeaponPhase::Preparing && state.phase != WeaponPhase::Ready)
    {
        return std::unexpected("weapon target can only be assigned while Preparing or Ready");
    }

    const auto validTrack = ValidateTrackForWeapon(definition, track);
    if (!validTrack)
    {
        return std::unexpected(validTrack.error());
    }

    state.targetTrackId = track.trackId;
    return {};
}

[[nodiscard]] inline std::expected<void, std::string> LaunchWeapon(
    const WeaponDefinition& definition, WeaponRuntimeState& state, const double simulationTimeSeconds)
{
    if (state.definitionId != definition.id)
    {
        return std::unexpected("weapon runtime does not match definition");
    }
    const auto validTime = ValidateWeaponTime(state, simulationTimeSeconds);
    if (!validTime)
    {
        return std::unexpected(validTime.error());
    }
    if (state.phase != WeaponPhase::Ready)
    {
        return std::unexpected("weapon must be Ready before launch");
    }
    if (!state.targetTrackId.has_value())
    {
        return std::unexpected("weapon launch requires an assigned perceived-world track");
    }

    state.phase = WeaponPhase::Launched;
    state.phaseEnteredTimeSeconds = simulationTimeSeconds;
    state.lastUpdateTimeSeconds = simulationTimeSeconds;
    return {};
}
} // namespace DeepRun::Weapons
