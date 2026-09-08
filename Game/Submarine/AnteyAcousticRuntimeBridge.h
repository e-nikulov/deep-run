#pragma once

#include "Game/Submarine/AnteyAcousticModel.h"
#include "Simulation/Marine/PropulsionSystem.h"
#include "Simulation/Marine/WaterBody.h"

#include <cmath>
#include <expected>
#include <string>

namespace DeepRun::Game::Submarine
{
// Narrow composition boundary from existing authoritative simulation snapshots into the production Antey
// acoustic policy. It deliberately accepts value copies only: no PhysicsWorld/Jolt handle, WaterBody owner,
// renderer, input or audio backend crosses this boundary.
[[nodiscard]] inline std::expected<AnteyAcousticRuntimeState, std::string> ComposeAnteyAcousticRuntimeState(
    const Physics::PhysicsBodyState& bodyState,
    const Marine::WaterSurfaceSample& waterSample,
    const Marine::PropulsionState& propulsionState)
{
    if (!bodyState.position.IsFinite() || !bodyState.linearVelocity.IsFinite() ||
        !std::isfinite(waterSample.signedDepthMeters) || !std::isfinite(propulsionState.shaftRpm))
    {
        return std::unexpected("Antey live acoustic composition inputs must be finite");
    }

    return AnteyAcousticRuntimeState{
        .bodyReferencePositionMeters = bodyState.position,
        .linearVelocityMetersPerSecond = bodyState.linearVelocity,
        .shaftRpm = propulsionState.shaftRpm,
        .signedDepthMeters = waterSample.signedDepthMeters};
}
}
