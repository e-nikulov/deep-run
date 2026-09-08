#pragma once

#include "Simulation/Acoustics/AcousticEnvironment.h"
#include "Simulation/Acoustics/AcousticWorld.h"

namespace DeepRun::Tests
{
namespace M4EnvironmentDetail
{
[[nodiscard]] inline Acoustics::AcousticSpectrum UniformSpectrum(const float levelDb)
{
    return Acoustics::AcousticSpectrum{.levelDb = {levelDb, levelDb, levelDb, levelDb}};
}

[[nodiscard]] inline Acoustics::AcousticReceiver Receiver(const Physics::PhysicsVector3& position)
{
    return Acoustics::AcousticReceiver{
        .sensorId = "MGK540_BOW_ARRAY",
        .positionMeters = position,
        .ambientNoiseLevelDb = UniformSpectrum(35.0F),
        .selfNoiseLevelDb = UniformSpectrum(35.0F),
        .sensitivityDb = UniformSpectrum(0.0F),
        .minimumPeakSnrDb = 3.0F};
}
}

[[nodiscard]] inline bool RunM4EnvironmentChecks()
{
    using namespace Acoustics;
    using namespace M4EnvironmentDetail;

    const auto world = AcousticWorld::Create({});
    if (!world)
    {
        return false;
    }

    const AcousticEmission crossingEmission{
        .positionMeters = {3000.0F, -40.0F, 0.0F},
        .sourceLevelDb = UniformSpectrum(210.0F),
        .emissionTimeSeconds = 0.0};
    const AcousticReceiver deepReceiver = Receiver({0.0F, -200.0F, 0.0F});

    const auto crossingEnvironment = EvaluateAcousticEnvironmentPath(
        crossingEmission.positionMeters, deepReceiver.positionMeters, 0.0F, 0.5F);
    if (!crossingEnvironment || !crossingEnvironment->crossedThermocline ||
        !crossingEnvironment->terrainAttenuated || crossingEnvironment->confidenceMultiplier >= 1.0F)
    {
        return false;
    }

    const auto baseline = world->CollectPassiveDirectObservation(crossingEmission, deepReceiver, 10.0);
    const auto attenuated = world->CollectPassiveDirectObservation(
        crossingEmission, deepReceiver, 10.0, *crossingEnvironment);
    if (!baseline || !baseline->has_value() || !attenuated || !attenuated->has_value())
    {
        return false;
    }
    if (!(attenuated->value().receivedLevelDb[AcousticBand::High] <
          baseline->value().receivedLevelDb[AcousticBand::High]) ||
        !(attenuated->value().confidence < baseline->value().confidence))
    {
        return false;
    }

    // Full coarse terrain obstruction remains a finite attenuation model, not binary acoustic visibility.
    const AcousticEmission loudSameLayerEmission{
        .positionMeters = {1500.0F, -40.0F, 0.0F},
        .sourceLevelDb = UniformSpectrum(220.0F),
        .emissionTimeSeconds = 0.0};
    const AcousticReceiver sameLayerReceiver = Receiver({0.0F, -40.0F, 0.0F});
    const auto fullTerrain = EvaluateAcousticEnvironmentPath(
        loudSameLayerEmission.positionMeters, sameLayerReceiver.positionMeters, 0.0F, 1.0F);
    if (!fullTerrain || !fullTerrain->terrainAttenuated || fullTerrain->crossedThermocline)
    {
        return false;
    }
    const auto throughTerrain = world->CollectPassiveDirectObservation(
        loudSameLayerEmission, sameLayerReceiver, 10.0, *fullTerrain);
    if (!throughTerrain || !throughTerrain->has_value())
    {
        return false;
    }

    const auto clearShallow = EvaluateAcousticEnvironmentPath(
        {100.0F, -20.0F, 0.0F}, {0.0F, -30.0F, 0.0F}, 0.0F, 0.0F);
    if (!clearShallow || clearShallow->crossedThermocline || clearShallow->terrainAttenuated ||
        clearShallow->confidenceMultiplier != 1.0F)
    {
        return false;
    }

    if (EvaluateAcousticEnvironmentPath({}, {}, 0.0F, -0.1F))
    {
        return false;
    }

    AcousticPropagationModifiers invalidModifiers{};
    invalidModifiers.additionalTransmissionLossDb.levelDb[0] = -1.0F;
    const auto invalidResult = world->CollectPassiveDirectObservation(
        loudSameLayerEmission, sameLayerReceiver, 10.0, invalidModifiers);
    return !invalidResult && invalidResult.error().code == AcousticErrorCode::InvalidPropagationModifiers;
}
}
