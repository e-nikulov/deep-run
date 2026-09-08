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
// this region has NO_GEOMETRIC_ANCHOR_AUTHORED, so M4 must not invent a fake bow-array transform.
inline constexpr std::string_view AnteyMainPassiveArraySensorId = "MGK540_BOW_ARRAY";

// Plain authoritative runtime inputs. This composition layer knows nothing about Jolt handles, GLB nodes,
// renderer state, input devices, AudioEngine/miniaudio, or authoring hierarchy. signedDepthMeters follows the
// WaterBody contract: positive is below the reference surface.
struct AnteyAcousticRuntimeState final
{
    Physics::PhysicsVector3 bodyReferencePositionMeters{};
    Physics::PhysicsVector3 linearVelocityMetersPerSecond{};
    float shaftRpm = 0.0F;
    float signedDepthMeters = 0.0F;
};

struct AnteyAcousticSnapshot final
{
    Acoustics::AcousticEmitter emitter{};
    Acoustics::AcousticReceiver passiveReceiver{};
    float cavitationIntensity = 0.0F;
    float signedDepthMeters = 0.0F;
};

// Builds the current coarse gameplay signature and passive receiver state for the production Antey runtime.
// Numeric values are authored gameplay tuning, not measured/classified Project 949A acoustic data. Cavitation
// is deliberately a smooth gameplay function of RPM, vessel speed and depth rather than an exact physical
// threshold. Until a geometric sonar anchor is authored, the receiver uses the authoritative body reference.
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
    constexpr Acoustics::AcousticSpectrum fullCavitationSourceBoostDb{
        .levelDb = {8.0F, 12.0F, 18.0F, 24.0F}};
    constexpr Acoustics::AcousticSpectrum fullCavitationSelfNoiseBoostDb{
        .levelDb = {4.0F, 6.0F, 9.0F, 12.0F}};
    constexpr float maximumGameplayShaftRpm = 180.0F;
    constexpr float minimumPassivePeakSnrDb = 3.0F;

    if (!state.bodyReferencePositionMeters.IsFinite() || !state.linearVelocityMetersPerSecond.IsFinite() ||
        !std::isfinite(state.shaftRpm) || !std::isfinite(state.signedDepthMeters) ||
        !ambientNoiseLevelDb.IsFinite())
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

    // Gameplay-only cavitation approximation. More RPM/speed increases cavitation; increasing depth reduces
    // it but never creates a magical zero-noise deep state. Values are intentionally not real submarine data.
    const float rpmDrive = (std::max)(0.0F, (normalizedRpm - 0.55F) / 0.55F);
    const float speedDrive = (std::max)(0.0F, (speedMetersPerSecond - 7.0F) / 12.0F);
    const float shallowFactor = std::clamp(1.0F - (std::max)(state.signedDepthMeters, 0.0F) / 300.0F, 0.20F, 1.0F);
    const float cavitationIntensity = std::clamp((0.65F * rpmDrive + 0.35F * speedDrive) * shallowFactor, 0.0F, 1.0F);

    Acoustics::AcousticSpectrum sourceLevel = baseSourceLevelDb;
    Acoustics::AcousticSpectrum selfNoise = baseSelfNoiseLevelDb;
    for (std::size_t band = 0; band < Acoustics::AcousticBandCount; ++band)
    {
        const float bandWeight = 0.75F + 0.15F * static_cast<float>(band);
        sourceLevel.levelDb[band] += 18.0F * normalizedRpm * bandWeight + speedMetersPerSecond * bandWeight +
                                     fullCavitationSourceBoostDb.levelDb[band] * cavitationIntensity;
        selfNoise.levelDb[band] += 14.0F * normalizedRpm * bandWeight + speedMetersPerSecond * 1.25F * bandWeight +
                                   fullCavitationSelfNoiseBoostDb.levelDb[band] * cavitationIntensity;
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
    snapshot.cavitationIntensity = cavitationIntensity;
    snapshot.signedDepthMeters = state.signedDepthMeters;
    return snapshot;
}
}
