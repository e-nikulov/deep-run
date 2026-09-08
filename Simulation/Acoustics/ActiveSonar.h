#pragma once

#include "Simulation/Acoustics/AcousticWorld.h"

#include <algorithm>
#include <cmath>
#include <expected>
#include <limits>
#include <optional>
#include <string>

namespace DeepRun::Acoustics
{
// M4 active-sonar gameplay values. These are authored tuning parameters, not exact military sensor data.
struct ActiveAcousticPulse final
{
    Physics::PhysicsVector3 originMeters{};
    Physics::PhysicsVector3 forwardUnitVector{1.0F, 0.0F, 0.0F};
    AcousticSpectrum sourceLevelDb{.levelDb = {205.0F, 210.0F, 214.0F, 208.0F}};
    float beamHalfAngleRadians = 0.5235988F;
    double emissionTimeSeconds = 0.0;
};

// Authoritative reflector state is visible only inside simulation. No entity/type identity is propagated into
// the returned AcousticObservation; the echo exposes only measured evidence.
struct AcousticReflector final
{
    Physics::PhysicsVector3 positionMeters{};
    AcousticSpectrum reflectionLossDb{.levelDb = {16.0F, 13.0F, 11.0F, 14.0F}};
};

struct ActiveSonarConfig final
{
    float bearingUncertaintyRadians = 0.03490659F;
    float minimumRangeUncertaintyMeters = 5.0F;
    float fractionalRangeUncertainty = 0.02F;
};

[[nodiscard]] inline std::expected<AcousticEmission, std::string> MakeActiveTransmissionEmission(
    const ActiveAcousticPulse& pulse)
{
    if (!pulse.originMeters.IsFinite() || !pulse.forwardUnitVector.IsFinite() || !pulse.sourceLevelDb.IsFinite() ||
        !std::isfinite(pulse.beamHalfAngleRadians) || pulse.beamHalfAngleRadians <= 0.0F ||
        pulse.beamHalfAngleRadians > 3.1415927F || !std::isfinite(pulse.emissionTimeSeconds) ||
        pulse.emissionTimeSeconds < 0.0)
    {
        return std::unexpected("active acoustic pulse is invalid");
    }

    const double forwardLengthSquared =
        static_cast<double>(pulse.forwardUnitVector.x) * pulse.forwardUnitVector.x +
        static_cast<double>(pulse.forwardUnitVector.y) * pulse.forwardUnitVector.y +
        static_cast<double>(pulse.forwardUnitVector.z) * pulse.forwardUnitVector.z;
    if (!std::isfinite(forwardLengthSquared) || forwardLengthSquared <= 0.0)
    {
        return std::unexpected("active acoustic pulse direction must be finite and non-zero");
    }

    return AcousticEmission{
        .positionMeters = pulse.originMeters,
        .sourceLevelDb = pulse.sourceLevelDb,
        .emissionTimeSeconds = pulse.emissionTimeSeconds};
}

namespace ActiveSonarDetail
{
[[nodiscard]] inline bool ValidModifiers(const AcousticPropagationModifiers& modifiers) noexcept
{
    if (!modifiers.additionalTransmissionLossDb.IsFinite() || !std::isfinite(modifiers.confidenceMultiplier) ||
        modifiers.confidenceMultiplier < 0.0F || modifiers.confidenceMultiplier > 1.0F)
    {
        return false;
    }
    for (const float loss : modifiers.additionalTransmissionLossDb.levelDb)
    {
        if (loss < 0.0F)
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] inline float CombineIndependentNoiseLevelsDb(const float firstDb, const float secondDb)
{
    const double high = static_cast<double>(std::max(firstDb, secondDb));
    const double low = static_cast<double>(std::min(firstDb, secondDb));
    return static_cast<float>(high + 10.0 * std::log10(1.0 + std::pow(10.0, (low - high) / 10.0)));
}

[[nodiscard]] inline double Distance(const Physics::PhysicsVector3& first, const Physics::PhysicsVector3& second)
{
    const double dx = static_cast<double>(second.x) - first.x;
    const double dy = static_cast<double>(second.y) - first.y;
    const double dz = static_cast<double>(second.z) - first.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

[[nodiscard]] inline bool InsideBeam(
    const ActiveAcousticPulse& pulse,
    const Physics::PhysicsVector3& reflectorPositionMeters,
    const double distanceMeters)
{
    if (distanceMeters <= 1.0e-6)
    {
        return true;
    }

    const double forwardLength = std::sqrt(
        static_cast<double>(pulse.forwardUnitVector.x) * pulse.forwardUnitVector.x +
        static_cast<double>(pulse.forwardUnitVector.y) * pulse.forwardUnitVector.y +
        static_cast<double>(pulse.forwardUnitVector.z) * pulse.forwardUnitVector.z);
    const double dx = (static_cast<double>(reflectorPositionMeters.x) - pulse.originMeters.x) / distanceMeters;
    const double dy = (static_cast<double>(reflectorPositionMeters.y) - pulse.originMeters.y) / distanceMeters;
    const double dz = (static_cast<double>(reflectorPositionMeters.z) - pulse.originMeters.z) / distanceMeters;
    const double dot =
        dx * (static_cast<double>(pulse.forwardUnitVector.x) / forwardLength) +
        dy * (static_cast<double>(pulse.forwardUnitVector.y) / forwardLength) +
        dz * (static_cast<double>(pulse.forwardUnitVector.z) / forwardLength);
    return dot >= std::cos(static_cast<double>(pulse.beamHalfAngleRadians));
}
}

// First bounded M4 active echo model: one directional monostatic pulse, one simplified reflector and two direct
// propagation legs. The transmitter and receiver must be colocated because the initial range estimate is derived
// from round-trip timing. Bistatic geometry can be added later without changing the observation boundary.
[[nodiscard]] inline std::expected<std::optional<AcousticObservation>, std::string>
CollectMonostaticActiveEchoObservation(
    const AcousticWorld& world,
    const ActiveAcousticPulse& pulse,
    const AcousticReflector& reflector,
    const AcousticReceiver& receiver,
    const double simulationTimeSeconds,
    const AcousticPropagationModifiers& outboundModifiers = {},
    const AcousticPropagationModifiers& returnModifiers = {},
    const ActiveSonarConfig& activeConfig = {})
{
    const auto transmission = MakeActiveTransmissionEmission(pulse);
    if (!transmission)
    {
        return std::unexpected(transmission.error());
    }
    if (!reflector.positionMeters.IsFinite() || !reflector.reflectionLossDb.IsFinite())
    {
        return std::unexpected("active acoustic reflector is invalid");
    }
    for (const float reflectionLoss : reflector.reflectionLossDb.levelDb)
    {
        if (reflectionLoss < 0.0F)
        {
            return std::unexpected("active acoustic reflection loss must be non-negative");
        }
    }
    if (receiver.sensorId.empty() || !receiver.positionMeters.IsFinite() || !receiver.ambientNoiseLevelDb.IsFinite() ||
        !receiver.selfNoiseLevelDb.IsFinite() || !receiver.sensitivityDb.IsFinite() ||
        !std::isfinite(receiver.minimumPeakSnrDb))
    {
        return std::unexpected("active acoustic receiver is invalid");
    }
    if (!std::isfinite(simulationTimeSeconds) || simulationTimeSeconds < 0.0 ||
        !ActiveSonarDetail::ValidModifiers(outboundModifiers) || !ActiveSonarDetail::ValidModifiers(returnModifiers) ||
        !std::isfinite(activeConfig.bearingUncertaintyRadians) || activeConfig.bearingUncertaintyRadians < 0.0F ||
        !std::isfinite(activeConfig.minimumRangeUncertaintyMeters) || activeConfig.minimumRangeUncertaintyMeters < 0.0F ||
        !std::isfinite(activeConfig.fractionalRangeUncertainty) || activeConfig.fractionalRangeUncertainty < 0.0F)
    {
        return std::unexpected("active sonar runtime/configuration input is invalid");
    }

    const double transmitterReceiverSeparation = ActiveSonarDetail::Distance(pulse.originMeters, receiver.positionMeters);
    if (!std::isfinite(transmitterReceiverSeparation) || transmitterReceiverSeparation > 0.01)
    {
        return std::unexpected("initial active echo model requires colocated transmitter and receiver");
    }

    const double outboundDistanceMeters = ActiveSonarDetail::Distance(pulse.originMeters, reflector.positionMeters);
    if (!std::isfinite(outboundDistanceMeters))
    {
        return std::unexpected("active sonar outbound distance is non-finite");
    }
    if (outboundDistanceMeters > world.Config().maxPropagationDistanceMeters ||
        !ActiveSonarDetail::InsideBeam(pulse, reflector.positionMeters, outboundDistanceMeters))
    {
        return std::optional<AcousticObservation>{};
    }

    const double returnDistanceMeters = ActiveSonarDetail::Distance(reflector.positionMeters, receiver.positionMeters);
    if (!std::isfinite(returnDistanceMeters) || returnDistanceMeters > world.Config().maxPropagationDistanceMeters)
    {
        return std::optional<AcousticObservation>{};
    }

    const double arrivalTimeSeconds = pulse.emissionTimeSeconds +
        (outboundDistanceMeters + returnDistanceMeters) /
            static_cast<double>(world.Config().effectiveSoundSpeedMetersPerSecond);
    if (!std::isfinite(arrivalTimeSeconds))
    {
        return std::unexpected("active sonar echo arrival time is non-finite");
    }
    if (simulationTimeSeconds < arrivalTimeSeconds)
    {
        return std::optional<AcousticObservation>{};
    }

    const double outboundLossDistanceMeters = std::max(
        outboundDistanceMeters, static_cast<double>(world.Config().referenceDistanceMeters));
    const double returnLossDistanceMeters = std::max(
        returnDistanceMeters, static_cast<double>(world.Config().referenceDistanceMeters));
    const double outboundSpreadingLossDb = static_cast<double>(world.Config().spreadingLossDbPerDistanceDecade) *
        std::log10(outboundLossDistanceMeters / world.Config().referenceDistanceMeters);
    const double returnSpreadingLossDb = static_cast<double>(world.Config().spreadingLossDbPerDistanceDecade) *
        std::log10(returnLossDistanceMeters / world.Config().referenceDistanceMeters);
    if (!std::isfinite(outboundSpreadingLossDb) || !std::isfinite(returnSpreadingLossDb))
    {
        return std::unexpected("active sonar transmission loss is non-finite");
    }

    AcousticObservation observation{};
    observation.kind = AcousticObservationKind::ActiveEcho;
    observation.sensorId = receiver.sensorId;
    observation.observationTimeSeconds = simulationTimeSeconds;
    observation.arrivalTimeSeconds = arrivalTimeSeconds;
    observation.measuredBearingRadians = static_cast<float>(std::atan2(
        static_cast<double>(reflector.positionMeters.y) - pulse.originMeters.y,
        static_cast<double>(reflector.positionMeters.x) - pulse.originMeters.x));
    observation.bearingUncertaintyRadians = activeConfig.bearingUncertaintyRadians;
    observation.estimatedRangeMeters = static_cast<float>(outboundDistanceMeters);
    observation.rangeUncertaintyMeters = std::max(
        activeConfig.minimumRangeUncertaintyMeters,
        static_cast<float>(outboundDistanceMeters) * activeConfig.fractionalRangeUncertainty);
    observation.peakSignalToNoiseDb = -std::numeric_limits<float>::infinity();
    observation.pathClass = AcousticPathClass::Direct;

    for (std::size_t band = 0; band < AcousticBandCount; ++band)
    {
        const double outboundAbsorptionDb = static_cast<double>(world.Config().absorptionDbPerKilometer[band]) *
            (outboundDistanceMeters / 1000.0);
        const double returnAbsorptionDb = static_cast<double>(world.Config().absorptionDbPerKilometer[band]) *
            (returnDistanceMeters / 1000.0);
        const double receivedDb = static_cast<double>(pulse.sourceLevelDb.levelDb[band]) -
            outboundSpreadingLossDb - returnSpreadingLossDb - outboundAbsorptionDb - returnAbsorptionDb -
            reflector.reflectionLossDb.levelDb[band] - outboundModifiers.additionalTransmissionLossDb.levelDb[band] -
            returnModifiers.additionalTransmissionLossDb.levelDb[band] + receiver.sensitivityDb.levelDb[band];
        const float noiseDb = ActiveSonarDetail::CombineIndependentNoiseLevelsDb(
            receiver.ambientNoiseLevelDb.levelDb[band], receiver.selfNoiseLevelDb.levelDb[band]);
        const double snrDb = receivedDb - noiseDb;
        if (!std::isfinite(receivedDb) || !std::isfinite(noiseDb) || !std::isfinite(snrDb))
        {
            return std::unexpected("active sonar received band result is non-finite");
        }
        observation.receivedLevelDb.levelDb[band] = static_cast<float>(receivedDb);
        observation.signalToNoiseDb.levelDb[band] = static_cast<float>(snrDb);
        observation.peakSignalToNoiseDb = std::max(observation.peakSignalToNoiseDb,
                                                   observation.signalToNoiseDb.levelDb[band]);
    }

    if (observation.peakSignalToNoiseDb < receiver.minimumPeakSnrDb)
    {
        return std::optional<AcousticObservation>{};
    }

    observation.confidence = std::clamp(
        (observation.peakSignalToNoiseDb - receiver.minimumPeakSnrDb) /
            world.Config().confidenceFullScaleSnrMarginDb,
        0.0F,
        1.0F) * outboundModifiers.confidenceMultiplier * returnModifiers.confidenceMultiplier;
    return std::optional<AcousticObservation>{observation};
}
}
