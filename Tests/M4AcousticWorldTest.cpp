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

[[nodiscard]] AcousticReceiver QuietReceiver(const PhysicsVector3 position = {})
{
    return AcousticReceiver{
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
    return NearlyEqual(value.measuredBearingRadians, quarterTurn) && !value.estimatedRangeMeters.has_value() &&
           !value.rangeUncertaintyMeters.has_value();
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
}

int main()
{
    const bool ok = PropagationDelayUsesSimulationTime() && FrequencyDependentAbsorptionIsApplied() &&
                    AmbientAndSelfNoiseGateDetection() && PassiveObservationPreservesKnowledgeBoundary() &&
                    PropagationIsBounded() && RepeatedEvaluationIsDeterministic() && InvalidConfigurationIsRejected();
    std::cout << (ok ? "M4-A ACOUSTIC WORLD: PASS\n" : "M4-A ACOUSTIC WORLD: FAIL\n");
    return ok ? 0 : 1;
}
