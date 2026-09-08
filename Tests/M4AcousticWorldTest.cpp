#include "Game/AcousticPlayground.h"
#include "Game/Submarine/AnteyAcousticModel.h"
#include "Simulation/Acoustics/AcousticWorld.h"

#include <cmath>
#include <iostream>

namespace
{
using namespace DeepRun::Acoustics;
using DeepRun::Physics::PhysicsVector3;

[[nodiscard]] bool NearlyEqual(const double first, const double second, const double tolerance = 1e-5)
{
    return std::abs(first - second) <= tolerance;
}

[[nodiscard]] AcousticSpectrum UniformSpectrum(const float levelDb)
{
    return AcousticSpectrum{.levelDb = {levelDb, levelDb, levelDb, levelDb}};
}

[[nodiscard]] AcousticSpectrum AmbientSpectrum()
{
    return AcousticSpectrum{.levelDb = {42.0F, 40.0F, 38.0F, 36.0F}};
}

[[nodiscard]] AcousticReceiver QuietReceiver(const PhysicsVector3 position = {})
{
    return AcousticReceiver{
        .sensorId = "test.passive.array",
        .positionMeters = position,
        .ambientNoiseLevelDb = UniformSpectrum(35.0F),
        .selfNoiseLevelDb = UniformSpectrum(35.0F),
        .sensitivityDb = UniformSpectrum(0.0F),
        .minimumPeakSnrDb = 3.0F};
}

[[nodiscard]] bool PropagationDelayUsesSimulationTime()
{
    const auto world = AcousticWorld::Create({});
    if (!world)
    {
        return false;
    }
    const AcousticEmission emission{
        .positionMeters = {1500.0F, 0.0F, 0.0F},
        .sourceLevelDb = UniformSpectrum(180.0F),
        .emissionTimeSeconds = 2.0};

    const auto beforeArrival = world->CollectPassiveDirectObservation(emission, QuietReceiver(), 2.999);
    const auto atArrival = world->CollectPassiveDirectObservation(emission, QuietReceiver(), 3.0);
    return beforeArrival && !*beforeArrival && atArrival && atArrival->has_value() &&
           NearlyEqual(atArrival->value().arrivalTimeSeconds, 3.0);
}

[[nodiscard]] bool FrequencyDependentAbsorptionIsApplied()
{
    const auto world = AcousticWorld::Create({});
    if (!world)
    {
        return false;
    }
    const AcousticEmission emission{
        .positionMeters = {10000.0F, 0.0F, 0.0F},
        .sourceLevelDb = UniformSpectrum(190.0F),
        .emissionTimeSeconds = 0.0};
    const auto observation = world->CollectPassiveDirectObservation(emission, QuietReceiver(), 10.0);
    if (!observation || !observation->has_value())
    {
        return false;
    }
    const auto& received = observation->value().receivedLevelDb;
    return received[AcousticBand::VeryLow] > received[AcousticBand::Low] &&
           received[AcousticBand::Low] > received[AcousticBand::Medium] &&
           received[AcousticBand::Medium] > received[AcousticBand::High];
}

[[nodiscard]] bool AmbientAndSelfNoiseGateDetection()
{
    const auto world = AcousticWorld::Create({});
    if (!world)
    {
        return false;
    }
    const AcousticEmission emission{
        .positionMeters = {1000.0F, 0.0F, 0.0F},
        .sourceLevelDb = UniformSpectrum(130.0F),
        .emissionTimeSeconds = 0.0};

    auto noisy = QuietReceiver();
    noisy.ambientNoiseLevelDb = UniformSpectrum(85.0F);
    noisy.selfNoiseLevelDb = UniformSpectrum(85.0F);
    const auto masked = world->CollectPassiveDirectObservation(emission, noisy, 1.0);
    const auto clear = world->CollectPassiveDirectObservation(emission, QuietReceiver(), 1.0);
    return masked && !*masked && clear && clear->has_value();
}

[[nodiscard]] bool PassiveObservationPreservesKnowledgeBoundary()
{
    const auto world = AcousticWorld::Create({});
    if (!world)
    {
        return false;
    }
    const AcousticEmission emission{
        .positionMeters = {1000.0F, 1000.0F, 0.0F},
        .sourceLevelDb = UniformSpectrum(180.0F),
        .emissionTimeSeconds = 0.0};
    const auto observation = world->CollectPassiveDirectObservation(emission, QuietReceiver(), 2.0);
    if (!observation || !observation->has_value())
    {
        return false;
    }
    constexpr double quarterTurn = 0.7853981633974483;
    const auto& value = observation->value();
    return value.sensorId == "test.passive.array" && NearlyEqual(value.measuredBearingRadians, quarterTurn) &&
           !value.estimatedRangeMeters.has_value() && !value.rangeUncertaintyMeters.has_value();
}

[[nodiscard]] bool PropagationIsBounded()
{
    const auto world = AcousticWorld::Create({});
    if (!world)
    {
        return false;
    }
    const AcousticEmission emission{
        .positionMeters = {100001.0F, 0.0F, 0.0F},
        .sourceLevelDb = UniformSpectrum(250.0F),
        .emissionTimeSeconds = 0.0};
    const auto observation = world->CollectPassiveDirectObservation(emission, QuietReceiver(), 100.0);
    return observation && !*observation;
}

[[nodiscard]] bool RepeatedEvaluationIsDeterministic()
{
    const auto world = AcousticWorld::Create({});
    if (!world)
    {
        return false;
    }
    const AcousticEmission emission{
        .positionMeters = {4200.0F, -700.0F, 25.0F},
        .sourceLevelDb = AcousticSpectrum{.levelDb = {176.0F, 171.0F, 165.0F, 158.0F}},
        .emissionTimeSeconds = 4.0};
    const auto receiver = QuietReceiver({125.0F, -80.0F, 0.0F});
    const auto first = world->CollectPassiveDirectObservation(emission, receiver, 10.0);
    const auto second = world->CollectPassiveDirectObservation(emission, receiver, 10.0);
    return first && second && *first == *second;
}

[[nodiscard]] bool InvalidConfigurationIsRejected()
{
    AcousticWorldConfig config{};
    config.effectiveSoundSpeedMetersPerSecond = 0.0F;
    const auto world = AcousticWorld::Create(config);
    return !world && world.error().code == AcousticErrorCode::InvalidConfiguration;
}

[[nodiscard]] bool ReceiverRequiresSensorIdentity()
{
    const auto world = AcousticWorld::Create({});
    if (!world)
    {
        return false;
    }
    auto receiver = QuietReceiver();
    receiver.sensorId.clear();
    const AcousticEmission emission{
        .positionMeters = {1000.0F, 0.0F, 0.0F},
        .sourceLevelDb = UniformSpectrum(180.0F),
        .emissionTimeSeconds = 0.0};
    const auto result = world->CollectPassiveDirectObservation(emission, receiver, 1.0);
    return !result && result.error().code == AcousticErrorCode::InvalidReceiver;
}

[[nodiscard]] bool AnteySensorIdentityUsesProductionSemanticRegion()
{
    const auto snapshot = DeepRun::Game::Submarine::BuildAnteyAcousticSnapshot({}, AmbientSpectrum());
    return snapshot &&
           snapshot->passiveReceiver.sensorId == DeepRun::Game::Submarine::AnteyMainPassiveArraySensorId &&
           snapshot->passiveReceiver.positionMeters == PhysicsVector3{};
}

[[nodiscard]] bool AnteyPropulsionRaisesEmittedSignature()
{
    DeepRun::Game::Submarine::AnteyAcousticRuntimeState stopped{};
    DeepRun::Game::Submarine::AnteyAcousticRuntimeState running{};
    running.shaftRpm = 180.0F;

    const auto quiet = DeepRun::Game::Submarine::BuildAnteyAcousticSnapshot(stopped, AmbientSpectrum());
    const auto loud = DeepRun::Game::Submarine::BuildAnteyAcousticSnapshot(running, AmbientSpectrum());
    if (!quiet || !loud)
    {
        return false;
    }
    for (std::size_t band = 0; band < AcousticBandCount; ++band)
    {
        if (!(loud->emitter.continuousSourceLevelDb.levelDb[band] > quiet->emitter.continuousSourceLevelDb.levelDb[band]))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool AnteySpeedRaisesPassiveSelfNoise()
{
    DeepRun::Game::Submarine::AnteyAcousticRuntimeState stopped{};
    DeepRun::Game::Submarine::AnteyAcousticRuntimeState moving{};
    moving.linearVelocityMetersPerSecond = {10.0F, 0.0F, 0.0F};

    const auto quiet = DeepRun::Game::Submarine::BuildAnteyAcousticSnapshot(stopped, AmbientSpectrum());
    const auto noisy = DeepRun::Game::Submarine::BuildAnteyAcousticSnapshot(moving, AmbientSpectrum());
    if (!quiet || !noisy)
    {
        return false;
    }
    for (std::size_t band = 0; band < AcousticBandCount; ++band)
    {
        if (!(noisy->passiveReceiver.selfNoiseLevelDb.levelDb[band] >
              quiet->passiveReceiver.selfNoiseLevelDb.levelDb[band]))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool AnteyRuntimeKinematicsReachAcousticSnapshot()
{
    DeepRun::Game::Submarine::AnteyAcousticRuntimeState state{};
    state.bodyReferencePositionMeters = {125.0F, -90.0F, 0.0F};
    state.linearVelocityMetersPerSecond = {4.0F, -0.5F, 0.0F};
    state.shaftRpm = 72.0F;

    const auto snapshot = DeepRun::Game::Submarine::BuildAnteyAcousticSnapshot(state, AmbientSpectrum());
    return snapshot && snapshot->emitter.positionMeters == state.bodyReferencePositionMeters &&
           snapshot->emitter.velocityMetersPerSecond == state.linearVelocityMetersPerSecond &&
           snapshot->passiveReceiver.positionMeters == state.bodyReferencePositionMeters;
}

[[nodiscard]] bool AnteyRejectsNonFiniteRuntimeState()
{
    DeepRun::Game::Submarine::AnteyAcousticRuntimeState state{};
    state.shaftRpm = std::nanf("");
    return !DeepRun::Game::Submarine::BuildAnteyAcousticSnapshot(state, AmbientSpectrum());
}

[[nodiscard]] bool AcousticPlaygroundClosesPassiveVerticalSlice()
{
    const auto playground = DeepRun::Game::AcousticPlayground::Create();
    if (!playground)
    {
        return false;
    }

    DeepRun::Game::Submarine::AnteyAcousticRuntimeState player{};
    player.bodyReferencePositionMeters = {0.0F, -100.0F, 0.0F};

    // Synthetic source is 4,500 m away and emits at t=0.5. At 1,500 m/s it must arrive at t=3.5.
    const auto before = playground->CollectSyntheticPassiveObservation(player, AmbientSpectrum(), 3.499);
    const auto arrived = playground->CollectSyntheticPassiveObservation(player, AmbientSpectrum(), 3.5);
    if (!before || before->has_value() || !arrived || !arrived->has_value())
    {
        return false;
    }

    const AcousticObservation& observation = **arrived;
    return observation.sensorId == DeepRun::Game::Submarine::AnteyMainPassiveArraySensorId &&
           NearlyEqual(observation.arrivalTimeSeconds, 3.5) &&
           NearlyEqual(observation.measuredBearingRadians, 0.0) &&
           !observation.estimatedRangeMeters.has_value() && !observation.rangeUncertaintyMeters.has_value();
}
}

int main()
{
    const bool ok = PropagationDelayUsesSimulationTime() && FrequencyDependentAbsorptionIsApplied() &&
                    AmbientAndSelfNoiseGateDetection() && PassiveObservationPreservesKnowledgeBoundary() &&
                    PropagationIsBounded() && RepeatedEvaluationIsDeterministic() && InvalidConfigurationIsRejected() &&
                    ReceiverRequiresSensorIdentity() && AnteySensorIdentityUsesProductionSemanticRegion() &&
                    AnteyPropulsionRaisesEmittedSignature() && AnteySpeedRaisesPassiveSelfNoise() &&
                    AnteyRuntimeKinematicsReachAcousticSnapshot() && AnteyRejectsNonFiniteRuntimeState() &&
                    AcousticPlaygroundClosesPassiveVerticalSlice();
    std::cout << (ok ? "M4-A/A.1 PASSIVE VERTICAL SLICE: PASS\n" : "M4-A/A.1 PASSIVE VERTICAL SLICE: FAIL\n");
    return ok ? 0 : 1;
}
