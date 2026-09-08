#pragma once

#include "Game/Submarine/AnteyAcousticRuntimeBridge.h"

#include <cmath>

namespace DeepRun::Tests
{
[[nodiscard]] inline bool RunM4AnteyRuntimeBridgeChecks()
{
    Physics::PhysicsBodyState body{};
    body.position = {125.0F, -90.0F, 0.0F};
    body.linearVelocity = {6.0F, -0.25F, 0.0F};

    Marine::WaterSurfaceSample water{};
    water.signedDepthMeters = 90.0F;
    Marine::PropulsionState propulsion{};
    propulsion.shaftRpm = 96.0F;

    const auto composed = Game::Submarine::ComposeAnteyAcousticRuntimeState(body, water, propulsion);
    if (!composed || composed->bodyReferencePositionMeters != body.position ||
        composed->linearVelocityMetersPerSecond != body.linearVelocity ||
        composed->shaftRpm != propulsion.shaftRpm || composed->signedDepthMeters != water.signedDepthMeters)
    {
        return false;
    }

    body.position.x = std::nanf("");
    return !Game::Submarine::ComposeAnteyAcousticRuntimeState(body, water, propulsion);
}
}
