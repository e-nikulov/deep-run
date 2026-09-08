#include "Simulation/Acoustics/AcousticWorld.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace DeepRun::Acoustics
{
namespace
{
[[nodiscard]] bool IsFiniteNonNegative(const float value) noexcept
{
    return std::isfinite(value) && value >= 0.0F;
}

[[nodiscard]] float CombineIndependentNoiseLevelsDb(const float firstDb, const float secondDb)
{
    const double high = static_cast<double>(std::max(firstDb, secondDb));
    const double low = static_cast<double>(std::min(firstDb, secondDb));
    const double combined = high + 10.0 * std::log10(1.0 + std::pow(10.0, (low - high) / 10.0));
    return static_cast<float>(combined);
}

[[nodiscard]] AcousticError MakeError(const AcousticErrorCode code, std::string message)
{
    return AcousticError{.code = code, .message = std::move(message)};
}
}

std::expected<AcousticWorld, AcousticError> AcousticWorld::Create(const AcousticWorldConfig& config)
{
    if (!std::isfinite(config.effectiveSoundSpeedMetersPerSecond) || config.effectiveSoundSpeedMetersPerSecond <= 0.0F)
    {
        return std::unexpected(MakeError(AcousticErrorCode::InvalidConfiguration,
                                         "effective sound speed must be finite and positive"));
    }
    if (!std::isfinite(config.referenceDistanceMeters) || config.referenceDistanceMeters <= 0.0F)
    {
        return std::unexpected(MakeError(AcousticErrorCode::InvalidConfiguration,
                                         "reference distance must be finite and positive"));
    }
    if (!std::isfinite(config.maxPropagationDistanceMeters) ||
        config.maxPropagationDistanceMeters < config.referenceDistanceMeters)
    {
        return std::unexpected(MakeError(AcousticErrorCode::InvalidConfiguration,
                                         "maximum propagation distance must be finite and at least the reference distance"));
    }
    if (!IsFiniteNonNegative(config.spreadingLossDbPerDistanceDecade))
    {
        return std::unexpected(MakeError(AcousticErrorCode::InvalidConfiguration,
                                         "spreading loss must be finite and non-negative"));
    }
    for (const float absorption : config.absorptionDbPerKilometer)
    {
        if (!IsFiniteNonNegative(absorption))
        {
            return std::unexpected(MakeError(AcousticErrorCode::InvalidConfiguration,
                                             "band absorption must be finite and non-negative"));
        }
    }
    if (!IsFiniteNonNegative(config.passiveBearingUncertaintyRadians))
    {
        return std::unexpected(MakeError(AcousticErrorCode::InvalidConfiguration,
                                         "bearing uncertainty must be finite and non-negative"));
    }
    if (!std::isfinite(config.confidenceFullScaleSnrMarginDb) || config.confidenceFullScaleSnrMarginDb <= 0.0F)
    {
        return std::unexpected(MakeError(AcousticErrorCode::InvalidConfiguration,
                                         "confidence SNR margin must be finite and positive"));
    }
    return AcousticWorld(config);
}

const AcousticWorldConfig& AcousticWorld::Config() const noexcept
{
    return config_;
}

std::expected<std::optional<AcousticObservation>, AcousticError> AcousticWorld::CollectPassiveDirectObservation(
    const AcousticEmission& emission,
    const AcousticReceiver& receiver,
    const double simulationTimeSeconds) const
{
    if (!std::isfinite(simulationTimeSeconds) || simulationTimeSeconds < 0.0)
    {
        return std::unexpected(MakeError(AcousticErrorCode::InvalidSimulationTime,
                                         "simulation time must be finite and non-negative"));
    }
    if (!emission.positionMeters.IsFinite() || !emission.sourceLevelDb.IsFinite() ||
        !std::isfinite(emission.emissionTimeSeconds) || emission.emissionTimeSeconds < 0.0)
    {
        return std::unexpected(MakeError(AcousticErrorCode::InvalidEmission, "acoustic emission is invalid"));
    }
    if (!receiver.positionMeters.IsFinite() || !receiver.ambientNoiseLevelDb.IsFinite() ||
        !receiver.selfNoiseLevelDb.IsFinite() || !receiver.sensitivityDb.IsFinite() ||
        !std::isfinite(receiver.minimumPeakSnrDb))
    {
        return std::unexpected(MakeError(AcousticErrorCode::InvalidReceiver, "acoustic receiver is invalid"));
    }

    const double dx = static_cast<double>(emission.positionMeters.x) - static_cast<double>(receiver.positionMeters.x);
    const double dy = static_cast<double>(emission.positionMeters.y) - static_cast<double>(receiver.positionMeters.y);
    const double dz = static_cast<double>(emission.positionMeters.z) - static_cast<double>(receiver.positionMeters.z);
    const double distanceMeters = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (!std::isfinite(distanceMeters))
    {
        return std::unexpected(MakeError(AcousticErrorCode::NonFiniteResult, "direct-path distance is non-finite"));
    }
    if (distanceMeters > static_cast<double>(config_.maxPropagationDistanceMeters))
    {
        return std::optional<AcousticObservation>{};
    }

    const double arrivalTimeSeconds = emission.emissionTimeSeconds +
                                      distanceMeters / static_cast<double>(config_.effectiveSoundSpeedMetersPerSecond);
    if (!std::isfinite(arrivalTimeSeconds))
    {
        return std::unexpected(MakeError(AcousticErrorCode::NonFiniteResult, "arrival time is non-finite"));
    }
    if (simulationTimeSeconds < arrivalTimeSeconds)
    {
        return std::optional<AcousticObservation>{};
    }

    const double lossDistanceMeters = std::max(distanceMeters, static_cast<double>(config_.referenceDistanceMeters));
    const double spreadingLossDb = static_cast<double>(config_.spreadingLossDbPerDistanceDecade) *
                                   std::log10(lossDistanceMeters / static_cast<double>(config_.referenceDistanceMeters));
    if (!std::isfinite(spreadingLossDb))
    {
        return std::unexpected(MakeError(AcousticErrorCode::NonFiniteResult, "spreading loss is non-finite"));
    }

    AcousticObservation observation{};
    observation.observationTimeSeconds = simulationTimeSeconds;
    observation.arrivalTimeSeconds = arrivalTimeSeconds;
    observation.measuredBearingRadians = static_cast<float>(std::atan2(dy, dx));
    observation.bearingUncertaintyRadians = config_.passiveBearingUncertaintyRadians;
    observation.pathClass = AcousticPathClass::Direct;
    observation.peakSignalToNoiseDb = -std::numeric_limits<float>::infinity();

    for (std::size_t bandIndex = 0; bandIndex < AcousticBandCount; ++bandIndex)
    {
        const double absorptionLossDb = static_cast<double>(config_.absorptionDbPerKilometer[bandIndex]) *
                                        (distanceMeters / 1000.0);
        const double receivedDb = static_cast<double>(emission.sourceLevelDb.levelDb[bandIndex]) - spreadingLossDb -
                                  absorptionLossDb + static_cast<double>(receiver.sensitivityDb.levelDb[bandIndex]);
        const float noiseDb = CombineIndependentNoiseLevelsDb(receiver.ambientNoiseLevelDb.levelDb[bandIndex],
                                                               receiver.selfNoiseLevelDb.levelDb[bandIndex]);
        const double snrDb = receivedDb - static_cast<double>(noiseDb);
        if (!std::isfinite(absorptionLossDb) || !std::isfinite(receivedDb) || !std::isfinite(noiseDb) ||
            !std::isfinite(snrDb))
        {
            return std::unexpected(MakeError(AcousticErrorCode::NonFiniteResult,
                                             "received acoustic band result is non-finite"));
        }
        observation.receivedLevelDb.levelDb[bandIndex] = static_cast<float>(receivedDb);
        observation.signalToNoiseDb.levelDb[bandIndex] = static_cast<float>(snrDb);
        observation.peakSignalToNoiseDb = std::max(observation.peakSignalToNoiseDb,
                                                   observation.signalToNoiseDb.levelDb[bandIndex]);
    }

    if (!std::isfinite(observation.measuredBearingRadians) || !std::isfinite(observation.peakSignalToNoiseDb))
    {
        return std::unexpected(MakeError(AcousticErrorCode::NonFiniteResult, "passive observation is non-finite"));
    }
    if (observation.peakSignalToNoiseDb < receiver.minimumPeakSnrDb)
    {
        return std::optional<AcousticObservation>{};
    }

    observation.confidence = std::clamp(
        (observation.peakSignalToNoiseDb - receiver.minimumPeakSnrDb) / config_.confidenceFullScaleSnrMarginDb,
        0.0F,
        1.0F);
    return std::optional<AcousticObservation>{observation};
}

AcousticWorld::AcousticWorld(AcousticWorldConfig config)
    : config_(std::move(config))
{
}
}
