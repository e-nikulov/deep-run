#pragma once

#include "Engine/Physics/PhysicsTypes.h"
#include "Simulation/Acoustics/AcousticTypes.h"

#include <algorithm>
#include <cmath>
#include <expected>
#include <string>
#include <string_view>

namespace DeepRun::Game::Submarine
{
// Canonical production semantic region from Antey.authoring.json. The content contract explicitly states that
// this region has NO_GEOMETRIC_ANCHOR_AUTHORED, so M4-A.1 must not invent a fake bow-array transform.
inline constexpr std::string_view AnteyMainPassiveArraySensorId = "MGK540_BOW_ARRAY";

// Plain authoritative runtime inputs. This composition layer knows nothing about Jolt handles, GLB nodes,
// renderer state, input devices, AudioEngine/miniaudio, or authoring hierarchy.
struct AnteyAcousticRuntimeState final
{
    Physics::PhysicsVector3 bodyReferencePositionMeters{};
    Physics::PhysicsVector3 linearVelocityMetersPerSecond{};
    float shaftRpm = 0.0F;
};

struct AnteyAcousticSnapshot final
{
    Acoustics::AcousticEmitter emitter{};
    Acoustics::AcousticReceiver passiveReceiver{};
};

// Builds the current coarse gameplay signature and passive receiver state for the production Antey runtime.
// The values are deliberately authored gameplay tuning, not measured/classified Project 949A acoustic data.
// Until a geometric sonar anchor is authored, receiver position is the authoritative body reference point;
// the semantic sensor identity remains MGK540_BOW_ARRAY without claiming nonexistent antenna geometry.
[[nodiscard]] inline std::expected<AnteyAcousticSnapshot, std::string> BuildAnteyAcousticSnapshot(
    const AnteyAcousticRuntimeState& state,
    const Acoustics::AcousticSpectrum& ambientNoiseLevelDb)
{
    constexpr Acoustics::AcousticSpectrum baseSourceLevelDb{
        .levelDb = {112.0F, 106.0F, 99.0F, 93.0F}};
    constexpr Acoustics::AcousticSpectrum baseSelfNoiseLevelDb{
        .levelDb = {37.0F, 38.0F, 40.0F, 42.0F}};
    constexpr Acoustics::AcousticSpectrum passiveSensitivityDb{
        .levelDb = {3.0F, 2.0F, 1.0F, 0.0F}};
    constexpr float maximumGameplayShaftRpm = 180.0F;
    constexpr float minimumPassivePeakSnrDb = 3.0F;

    if (!state.bodyReferencePositionMeters.IsFinite() || !state.linearVelocityMetersPerSecond.IsFinite() ||
        !std::isfinite(state.shaftRpm) || !ambientNoiseLevelDb.IsFinite())
    {
        return std::unexpected("Antey acoustic runtime inputs must be finite");
    }

    const Physics::PhysicsVector3& velocity = state.linearVelocityMetersPerSecond;
    const float speedMetersPerSecond = std::sqrt(
        velocity.x * velocity.x + velocity.y * velocity.y + velocity.z * velocity.z);
    if (!std::isfinite(speedMetersPerSecond))
    {
        return std::unexpected("Antey acoustic speed must be finite");
    }

    const float normalizedRpm = std::clamp(std::abs(state.shaftRpm) / maximumGameplayShaftRpm, 0.0F, 1.5F);
    Acoustics::AcousticSpectrum sourceLevel = baseSourceLevelDb;
    Acoustics::AcousticSpectrum selfNoise = baseSelfNoiseLevelDb;
    for (std::size_t band = 0; band < Acoustics::AcousticBandCount; ++band)
    {
        const float bandWeight = 0.75F + 0.15F * static_cast<float>(band);
        sourceLevel.levelDb[band] += 18.0F * normalizedRpm * bandWeight + speedMetersPerSecond * bandWeight;
        selfNoise.levelDb[band] += 14.0F * normalizedRpm * bandWeight + speedMetersPerSecond * 1.25F * bandWeight;
    }

    AnteyAcousticSnapshot snapshot{};
    snapshot.emitter.positionMeters = state.bodyReferencePositionMeters;
    snapshot.emitter.velocityMetersPerSecond = state.linearVelocityMetersPerSecond;
    snapshot.emitter.continuousSourceLevelDb = sourceLevel;

    snapshot.passiveReceiver.sensorId = std::string(AnteyMainPassiveArraySensorId);
    snapshot.passiveReceiver.positionMeters = state.bodyReferencePositionMeters;
    snapshot.passiveReceiver.ambientNoiseLevelDb = ambientNoiseLevelDb;
    snapshot.passiveReceiver.selfNoiseLevelDb = selfNoise;
    snapshot.passiveReceiver.sensitivityDb = passiveSensitivityDb;
    snapshot.passiveReceiver.minimumPeakSnrDb = minimumPassivePeakSnrDb;
    return snapshot;
}
}
