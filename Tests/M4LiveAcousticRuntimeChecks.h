#pragma once

#include "Game/AcousticPlaygroundRuntime.h"
#include "Tests/M4AnteyRuntimeBridgeChecks.h"

namespace DeepRun::Tests
{
[[nodiscard]] inline bool RunM4LiveAcousticRuntimeChecks()
{
    const auto runtimeCreated = Game::AcousticPlaygroundRuntime::Create();
    if (!runtimeCreated)
    {
        return false;
    }
    Game::AcousticPlaygroundRuntime runtime = *runtimeCreated;

    const Game::Submarine::AnteyAcousticRuntimeState state{
        .bodyReferencePositionMeters = {0.0F, -100.0F, 0.0F},
        .linearVelocityMetersPerSecond = {},
        .shaftRpm = 0.0F,
        .signedDepthMeters = 100.0F};
    const auto ownSnapshot = Game::Submarine::BuildAnteyAcousticSnapshot(
        state, Game::AcousticPlaygroundRuntime::AmbientNoiseLevelDb());
    if (!ownSnapshot)
    {
        return false;
    }

    const auto beforeArrival = runtime.Advance(*ownSnapshot, 0.25);
    if (!beforeArrival || beforeArrival->passiveObservation.has_value() ||
        !beforeArrival->contacts.empty() || !beforeArrival->tracks.empty())
    {
        return false;
    }

    const auto firstArrival = runtime.Advance(*ownSnapshot, 1.0);
    if (!firstArrival || !firstArrival->passiveObservation.has_value() ||
        firstArrival->passiveObservation->kind != Acoustics::AcousticObservationKind::PassiveReception ||
        firstArrival->passiveObservation->estimatedRangeMeters.has_value() ||
        !firstArrival->propagationModifiers.crossedThermocline ||
        firstArrival->contacts.size() != 1U || firstArrival->tracks.size() != 1U ||
        firstArrival->tracks.front().lifecycle != Perception::TrackLifecycleState::Tentative)
    {
        return false;
    }

    const auto confirmed = runtime.Advance(*ownSnapshot, 1.1);
    return confirmed && confirmed->passiveObservation.has_value() &&
           confirmed->contacts.size() == 1U && confirmed->tracks.size() == 1U &&
           confirmed->tracks.front().lifecycle == Perception::TrackLifecycleState::Confirmed &&
           confirmed->tracks.front().estimatedPositionMeters == std::nullopt &&
           confirmed->tracks.front().estimatedVelocityMetersPerSecond == std::nullopt &&
           RunM4AnteyRuntimeBridgeChecks();
}
}
