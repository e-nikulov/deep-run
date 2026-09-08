#pragma once

#include "Simulation/Acoustics/AcousticTypes.h"

#include <algorithm>
#include <cmath>
#include <expected>
#include <optional>
#include <string>

namespace DeepRun::Weapons
{
// M5-E acoustic countermeasure. A decoy is intentionally just a moving AcousticEmitter with an authored
// lifetime: normal acoustic propagation/perception decides whether anyone detects or tracks it. There is no
// special "fake target" bit and no source identity is added to observations.
struct AcousticDecoyDefinition final
{
    std::string id;
    Acoustics::AcousticSpectrum continuousSourceLevelDb{};
    Physics::PhysicsVector3 driftVelocityMetersPerSecond{};
    double activeLifetimeSeconds = 30.0;
};

struct AcousticDecoyRuntimeState final
{
    std::string definitionId;
    Acoustics::AcousticEmitter emitter{};
    double deploymentTimeSeconds = 0.0;
    double lastUpdateTimeSeconds = 0.0;
    bool active = true;
};

[[nodiscard]] inline std::expected<void, std::string> ValidateAcousticDecoyDefinition(
    const AcousticDecoyDefinition& definition)
{
    if (definition.id.empty() || !definition.continuousSourceLevelDb.IsFinite() ||
        !definition.driftVelocityMetersPerSecond.IsFinite() || !std::isfinite(definition.activeLifetimeSeconds) ||
        definition.activeLifetimeSeconds <= 0.0)
    {
        return std::unexpected("acoustic decoy definition is invalid");
    }
    return {};
}

[[nodiscard]] inline std::expected<AcousticDecoyRuntimeState, std::string> DeployAcousticDecoy(
    const AcousticDecoyDefinition& definition,
    const Physics::PhysicsVector3& positionMeters,
    const double simulationTimeSeconds)
{
    const auto valid = ValidateAcousticDecoyDefinition(definition);
    if (!valid)
    {
        return std::unexpected(valid.error());
    }
    const double activeUntilSeconds = simulationTimeSeconds + definition.activeLifetimeSeconds;
    if (!positionMeters.IsFinite() || !std::isfinite(simulationTimeSeconds) || simulationTimeSeconds < 0.0 ||
        !std::isfinite(activeUntilSeconds))
    {
        return std::unexpected("acoustic decoy deployment input is invalid");
    }

    return AcousticDecoyRuntimeState{
        .definitionId = definition.id,
        .emitter = Acoustics::AcousticEmitter{
            .positionMeters = positionMeters,
            .velocityMetersPerSecond = definition.driftVelocityMetersPerSecond,
            .continuousSourceLevelDb = definition.continuousSourceLevelDb},
        .deploymentTimeSeconds = simulationTimeSeconds,
        .lastUpdateTimeSeconds = simulationTimeSeconds,
        .active = true};
}

[[nodiscard]] inline std::expected<void, std::string> AdvanceAcousticDecoy(
    const AcousticDecoyDefinition& definition,
    AcousticDecoyRuntimeState& state,
    const double simulationTimeSeconds)
{
    const auto valid = ValidateAcousticDecoyDefinition(definition);
    if (!valid)
    {
        return std::unexpected(valid.error());
    }
    const double activeUntilSeconds = state.deploymentTimeSeconds + definition.activeLifetimeSeconds;
    const bool expectedActiveAtLastUpdate = state.lastUpdateTimeSeconds < activeUntilSeconds;
    if (state.definitionId != definition.id || !state.emitter.positionMeters.IsFinite() ||
        !state.emitter.velocityMetersPerSecond.IsFinite() || !state.emitter.continuousSourceLevelDb.IsFinite() ||
        !std::isfinite(activeUntilSeconds) || !std::isfinite(simulationTimeSeconds) ||
        simulationTimeSeconds < state.lastUpdateTimeSeconds || state.lastUpdateTimeSeconds < state.deploymentTimeSeconds ||
        state.active != expectedActiveAtLastUpdate)
    {
        return std::unexpected("acoustic decoy runtime is invalid or time-reversing");
    }

    const double movementStartSeconds = std::min(state.lastUpdateTimeSeconds, activeUntilSeconds);
    const double movementEndSeconds = std::min(simulationTimeSeconds, activeUntilSeconds);
    const double activeDeltaSeconds = std::max(0.0, movementEndSeconds - movementStartSeconds);
    if (activeDeltaSeconds > 0.0)
    {
        const float delta = static_cast<float>(activeDeltaSeconds);
        state.emitter.positionMeters.x += state.emitter.velocityMetersPerSecond.x * delta;
        state.emitter.positionMeters.y += state.emitter.velocityMetersPerSecond.y * delta;
        state.emitter.positionMeters.z += state.emitter.velocityMetersPerSecond.z * delta;
    }
    state.lastUpdateTimeSeconds = simulationTimeSeconds;
    state.active = simulationTimeSeconds < activeUntilSeconds;
    return {};
}

// Samples the ordinary emitter at the runtime's current SimulationTime. Back-dating an emission from a newer
// moved state is deliberately rejected, otherwise emissionTimeSeconds and emitter position would describe
// different authoritative moments. Inactive decoys simply have no emission to propagate.
[[nodiscard]] inline std::expected<std::optional<Acoustics::AcousticEmission>, std::string> SampleAcousticDecoyEmission(
    const AcousticDecoyDefinition& definition,
    const AcousticDecoyRuntimeState& state,
    const double emissionTimeSeconds)
{
    const auto valid = ValidateAcousticDecoyDefinition(definition);
    const double activeUntilSeconds = state.deploymentTimeSeconds + definition.activeLifetimeSeconds;
    const bool expectedActive = state.lastUpdateTimeSeconds < activeUntilSeconds;
    if (!valid || state.definitionId != definition.id || !state.emitter.positionMeters.IsFinite() ||
        !state.emitter.continuousSourceLevelDb.IsFinite() || !std::isfinite(activeUntilSeconds) ||
        !std::isfinite(emissionTimeSeconds) || emissionTimeSeconds != state.lastUpdateTimeSeconds ||
        state.lastUpdateTimeSeconds < state.deploymentTimeSeconds || state.active != expectedActive)
    {
        return std::unexpected("acoustic decoy emission sample is invalid or not current");
    }
    if (!state.active)
    {
        return std::optional<Acoustics::AcousticEmission>{};
    }

    return std::optional<Acoustics::AcousticEmission>{Acoustics::AcousticEmission{
        .positionMeters = state.emitter.positionMeters,
        .sourceLevelDb = state.emitter.continuousSourceLevelDb,
        .emissionTimeSeconds = emissionTimeSeconds}};
}
} // namespace DeepRun::Weapons
