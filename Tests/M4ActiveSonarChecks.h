#pragma once

#include "Simulation/Acoustics/ActiveSonar.h"
#include "Simulation/Perception/SensorObservation.h"

#include <cmath>

namespace DeepRun::Tests
{
namespace M4ActiveSonarDetail
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
        .ambientNoiseLevelDb = UniformSpectrum(30.0F),
        .selfNoiseLevelDb = UniformSpectrum(30.0F),
        .sensitivityDb = UniformSpectrum(0.0F),
        .minimumPeakSnrDb = 3.0F};
}
}

[[nodiscard]] inline bool RunM4ActiveSonarChecks()
{
    using namespace Acoustics;
    using namespace M4ActiveSonarDetail;

    const auto world = AcousticWorld::Create({});
    if (!world)
    {
        return false;
    }

    ActiveAcousticPulse pulse{};
    pulse.originMeters = {0.0F, -100.0F, 0.0F};
    pulse.forwardUnitVector = {1.0F, 0.0F, 0.0F};
    pulse.sourceLevelDb = UniformSpectrum(230.0F);
    pulse.beamHalfAngleRadians = 0.35F;
    pulse.emissionTimeSeconds = 1.0;

    AcousticReflector reflector{};
    reflector.positionMeters = {3000.0F, -100.0F, 0.0F};
    reflector.reflectionLossDb = UniformSpectrum(8.0F);

    const AcousticReceiver ownReceiver = Receiver(pulse.originMeters);

    // 3 km outbound + 3 km return at 1500 m/s => 4 s round trip; emitted at t=1 => echo at t=5.
    const auto beforeEcho = CollectMonostaticActiveEchoObservation(*world, pulse, reflector, ownReceiver, 4.999);
    const auto echo = CollectMonostaticActiveEchoObservation(*world, pulse, reflector, ownReceiver, 5.0);
    if (!beforeEcho || beforeEcho->has_value() || !echo || !echo->has_value())
    {
        return false;
    }

    const AcousticObservation& value = **echo;
    if (value.kind != AcousticObservationKind::ActiveEcho || value.sensorId != "MGK540_BOW_ARRAY" ||
        !value.estimatedRangeMeters.has_value() || !value.rangeUncertaintyMeters.has_value() ||
        std::abs(*value.estimatedRangeMeters - 3000.0F) > 0.01F ||
        std::abs(value.arrivalTimeSeconds - 5.0) > 1.0e-6)
    {
        return false;
    }

    const auto perceived = Perception::FromAcousticObservation(value);
    if (!perceived || perceived->modality != Perception::SensorModality::ActiveAcoustic ||
        !perceived->estimatedRangeMeters.has_value())
    {
        return false;
    }

    // An external passive receiver 1.5 km from the transmitter hears the outgoing ping at t=2, well before
    // the transmitting vessel receives its echo at t=5. This proves active transmission creates information
    // for other participants instead of being an invisible local query.
    const auto outgoingEmission = MakeActiveTransmissionEmission(pulse);
    if (!outgoingEmission)
    {
        return false;
    }
    const AcousticReceiver externalReceiver = Receiver({1500.0F, -100.0F, 0.0F});
    const auto externalBefore = world->CollectPassiveDirectObservation(*outgoingEmission, externalReceiver, 1.999);
    const auto externalHeard = world->CollectPassiveDirectObservation(*outgoingEmission, externalReceiver, 2.0);
    if (!externalBefore || externalBefore->has_value() || !externalHeard || !externalHeard->has_value() ||
        externalHeard->value().kind != AcousticObservationKind::PassiveReception ||
        externalHeard->value().estimatedRangeMeters.has_value())
    {
        return false;
    }

    // Directionality matters: a reflector behind a narrow forward beam does not generate an echo.
    AcousticReflector behind = reflector;
    behind.positionMeters = {-1000.0F, -100.0F, 0.0F};
    const auto behindEcho = CollectMonostaticActiveEchoObservation(*world, pulse, behind, ownReceiver, 10.0);
    if (!behindEcho || behindEcho->has_value())
    {
        return false;
    }

    // Environment modifiers apply independently to outbound and return legs and reduce echo confidence.
    AcousticPropagationModifiers outbound{};
    outbound.additionalTransmissionLossDb = UniformSpectrum(4.0F);
    outbound.confidenceMultiplier = 0.8F;
    AcousticPropagationModifiers returning{};
    returning.additionalTransmissionLossDb = UniformSpectrum(6.0F);
    returning.confidenceMultiplier = 0.7F;
    const auto clearEcho = CollectMonostaticActiveEchoObservation(*world, pulse, reflector, ownReceiver, 5.0);
    const auto attenuatedEcho = CollectMonostaticActiveEchoObservation(
        *world, pulse, reflector, ownReceiver, 5.0, outbound, returning);
    if (!clearEcho || !clearEcho->has_value() || !attenuatedEcho || !attenuatedEcho->has_value() ||
        !(attenuatedEcho->value().peakSignalToNoiseDb < clearEcho->value().peakSignalToNoiseDb) ||
        !(attenuatedEcho->value().confidence < clearEcho->value().confidence))
    {
        return false;
    }

    AcousticReceiver displacedReceiver = ownReceiver;
    displacedReceiver.positionMeters.x += 10.0F;
    return !CollectMonostaticActiveEchoObservation(*world, pulse, reflector, displacedReceiver, 5.0);
}
}
