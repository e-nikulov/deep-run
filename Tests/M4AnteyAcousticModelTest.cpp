#include "Game/Submarine/AnteyAcousticModel.h"

#include <cmath>
#include <iostream>

namespace
{
using namespace DeepRun;

[[nodiscard]] Acoustics::AcousticSpectrum Ambient()
{
    return Acoustics::AcousticSpectrum{.levelDb = {42.0F, 40.0F, 38.0F, 36.0F}};
}

[[nodiscard]] bool SensorIdentityUsesProductionSemanticRegion()
{
    const auto snapshot = Game::Submarine::BuildAnteyAcousticSnapshot({}, Ambient());
    return snapshot && snapshot->passiveReceiver.sensorId == Game::Submarine::AnteyMainPassiveArraySensorId &&
           snapshot->passiveReceiver.positionMeters == Physics::PhysicsVector3{};
}

[[nodiscard]] bool PropulsionRaisesEmittedSignature()
{
    Game::Submarine::AnteyAcousticRuntimeState stopped{};
    Game::Submarine::AnteyAcousticRuntimeState running{};
    running.shaftRpm = 180.0F;

    const auto quiet = Game::Submarine::BuildAnteyAcousticSnapshot(stopped, Ambient());
    const auto loud = Game::Submarine::BuildAnteyAcousticSnapshot(running, Ambient());
    if (!quiet || !loud)
    {
        return false;
    }

    for (std::size_t band = 0; band < Acoustics::AcousticBandCount; ++band)
    {
        if (!(loud->emitter.continuousSourceLevelDb.levelDb[band] > quiet->emitter.continuousSourceLevelDb.levelDb[band]))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool SpeedRaisesPassiveSelfNoise()
{
    Game::Submarine::AnteyAcousticRuntimeState stopped{};
    Game::Submarine::AnteyAcousticRuntimeState moving{};
    moving.linearVelocityMetersPerSecond = {10.0F, 0.0F, 0.0F};

    const auto quiet = Game::Submarine::BuildAnteyAcousticSnapshot(stopped, Ambient());
    const auto noisy = Game::Submarine::BuildAnteyAcousticSnapshot(moving, Ambient());
    if (!quiet || !noisy)
    {
        return false;
    }

    for (std::size_t band = 0; band < Acoustics::AcousticBandCount; ++band)
    {
        if (!(noisy->passiveReceiver.selfNoiseLevelDb.levelDb[band] >
              quiet->passiveReceiver.selfNoiseLevelDb.levelDb[band]))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool RuntimeKinematicsReachEmitterWithoutRenderDependency()
{
    Game::Submarine::AnteyAcousticRuntimeState state{};
    state.bodyReferencePositionMeters = {125.0F, -90.0F, 0.0F};
    state.linearVelocityMetersPerSecond = {4.0F, -0.5F, 0.0F};
    state.shaftRpm = 72.0F;

    const auto snapshot = Game::Submarine::BuildAnteyAcousticSnapshot(state, Ambient());
    return snapshot && snapshot->emitter.positionMeters == state.bodyReferencePositionMeters &&
           snapshot->emitter.velocityMetersPerSecond == state.linearVelocityMetersPerSecond &&
           snapshot->passiveReceiver.positionMeters == state.bodyReferencePositionMeters;
}

[[nodiscard]] bool NonFiniteRuntimeStateIsRejected()
{
    Game::Submarine::AnteyAcousticRuntimeState state{};
    state.shaftRpm = std::nanf("");
    return !Game::Submarine::BuildAnteyAcousticSnapshot(state, Ambient());
}
}

int main()
{
    const bool ok = SensorIdentityUsesProductionSemanticRegion() && PropulsionRaisesEmittedSignature() &&
                    SpeedRaisesPassiveSelfNoise() && RuntimeKinematicsReachEmitterWithoutRenderDependency() &&
                    NonFiniteRuntimeStateIsRejected();
    std::cout << (ok ? "M4-A.1 ANTEY ACOUSTIC MODEL: PASS\n" : "M4-A.1 ANTEY ACOUSTIC MODEL: FAIL\n");
    return ok ? 0 : 1;
}
