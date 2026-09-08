#include "Game/Submarine/AnteyAcousticModel.h"

#include <algorithm>
#include <cmath>

namespace DeepRun::Game::Submarine
{
namespace
{
constexpr Acoustics::AcousticSpectrum AnteyBaseSourceLevelDb{
    .levelDb = {112.0F, 106.0F, 99.0F, 93.0F}};
constexpr Acoustics::AcousticSpectrum AnteyBaseSelfNoiseLevelDb{
    .levelDb = {37.0F, 38.0F, 40.0F, 42.0F}};
constexpr Acoustics::AcousticSpectrum AnteyPassiveSensitivityDb{
    .levelDb = {3.0F, 2.0F, 1.0F, 0.0F}};
constexpr float AnteyMaximumGameplayShaftRpm = 180.0F;
constexpr float AnteyMinimumPassivePeakSnrDb = 3.0F;

[[nodiscard]] float SpeedMetersPerSecond(const Physics::PhysicsVector3& velocity) noexcept
{
    return std::sqrt(velocity.x * velocity.x + velocity.y * velocity.y + velocity.z * velocity.z);
}
}

std::expected<AnteyAcousticSnapshot, std::string> BuildAnteyAcousticSnapshot(
    const AnteyAcousticRuntimeState& state,
    const Acoustics::AcousticSpectrum& ambientNoiseLevelDb)
{
    if (!state.bodyReferencePositionMeters.IsFinite() || !state.linearVelocityMetersPerSecond.IsFinite() ||
        !std::isfinite(state.shaftRpm) || !ambientNoiseLevelDb.IsFinite())
    {
        return std::unexpected("Antey acoustic runtime inputs must be finite");
    }

    const float speedMetersPerSecond = SpeedMetersPerSecond(state.linearVelocityMetersPerSecond);
    if (!std::isfinite(speedMetersPerSecond))
    {
        return std::unexpected("Antey acoustic speed must be finite");
    }

    const float normalizedRpm = std::clamp(std::abs(state.shaftRpm) / AnteyMaximumGameplayShaftRpm, 0.0F, 1.5F);

    Acoustics::AcousticSpectrum sourceLevel = AnteyBaseSourceLevelDb;
    Acoustics::AcousticSpectrum selfNoise = AnteyBaseSelfNoiseLevelDb;
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
    snapshot.passiveReceiver.sensitivityDb = AnteyPassiveSensitivityDb;
    snapshot.passiveReceiver.minimumPeakSnrDb = AnteyMinimumPassivePeakSnrDb;
    return snapshot;
}
}
