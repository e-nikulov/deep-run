#pragma once

#include "Engine/Physics/PhysicsTypes.h"
#include "Simulation/Perception/SensorObservation.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace DeepRun::Perception
{
enum class ContactClassification
{
    Unknown,
};

struct Contact final
{
    std::uint64_t contactId = 0;
    ContactClassification classification = ContactClassification::Unknown;
    float lastMeasuredBearingRadians = 0.0F;
    float bearingUncertaintyRadians = 0.0F;
    float confidence = 0.0F;
    std::size_t observationCount = 0;
    double firstObservationTimeSeconds = 0.0;
    double lastObservationTimeSeconds = 0.0;
};

enum class TrackLifecycleState
{
    Tentative,
    Confirmed,
    Coasting,
    Lost,
};

// A Track is an estimate, never an authoritative target. Bearing-only passive observations leave position and
// velocity empty. Ranged observations may populate a spatial estimate only from perceived bearing/range plus
// the observing participant's own sensor position; hostile ground-truth position never crosses this boundary.
struct Track final
{
    std::uint64_t trackId = 0;
    std::uint64_t contactId = 0;
    TrackLifecycleState lifecycle = TrackLifecycleState::Tentative;
    std::optional<Physics::PhysicsVector3> estimatedPositionMeters{};
    std::optional<float> positionUncertaintyMeters{};
    std::optional<Physics::PhysicsVector3> estimatedVelocityMetersPerSecond{};
    float estimatedBearingRadians = 0.0F;
    float bearingUncertaintyRadians = 0.0F;
    float confidence = 0.0F;
    std::size_t observationCount = 0;
    double firstObservationTimeSeconds = 0.0;
    double lastObservationTimeSeconds = 0.0;
};

struct TrackManagerConfig final
{
    float associationGateRadians = 0.2617994F; // 15 degrees; gameplay tuning, not sensor truth.
    std::size_t observationsToConfirm = 2;
    double coastAfterSeconds = 5.0;
    double lostAfterSeconds = 20.0;
    float confidenceDecayPerSecond = 0.035F;
    float bearingUncertaintyGrowthRadiansPerSecond = 0.012F;
    float positionUncertaintyGrowthMetersPerSecond = 5.0F;
    std::size_t maximumTracks = 32;
};

class TrackManager final
{
public:
    [[nodiscard]] static std::expected<TrackManager, std::string> Create(const TrackManagerConfig& config = {})
    {
        if (!std::isfinite(config.associationGateRadians) || config.associationGateRadians <= 0.0F ||
            config.associationGateRadians > 3.1415927F || config.observationsToConfirm == 0U ||
            !std::isfinite(config.coastAfterSeconds) || config.coastAfterSeconds < 0.0 ||
            !std::isfinite(config.lostAfterSeconds) || config.lostAfterSeconds <= config.coastAfterSeconds ||
            !std::isfinite(config.confidenceDecayPerSecond) || config.confidenceDecayPerSecond < 0.0F ||
            !std::isfinite(config.bearingUncertaintyGrowthRadiansPerSecond) ||
            config.bearingUncertaintyGrowthRadiansPerSecond < 0.0F ||
            !std::isfinite(config.positionUncertaintyGrowthMetersPerSecond) ||
            config.positionUncertaintyGrowthMetersPerSecond < 0.0F || config.maximumTracks == 0U)
        {
            return std::unexpected("invalid TrackManager configuration");
        }
        return TrackManager(config);
    }

    [[nodiscard]] std::expected<std::uint64_t, std::string> IntegrateObservation(const SensorObservation& observation)
    {
        if (observation.sensorId.empty() || !std::isfinite(observation.observationTimeSeconds) ||
            observation.observationTimeSeconds < currentTimeSeconds_ || !std::isfinite(observation.measuredBearingRadians) ||
            !std::isfinite(observation.bearingUncertaintyRadians) || observation.bearingUncertaintyRadians < 0.0F ||
            !std::isfinite(observation.confidence) || observation.confidence < 0.0F || observation.confidence > 1.0F ||
            (observation.sensorPositionMeters && !observation.sensorPositionMeters->IsFinite()) ||
            (observation.estimatedRangeMeters &&
             (!std::isfinite(*observation.estimatedRangeMeters) || *observation.estimatedRangeMeters < 0.0F)) ||
            (observation.rangeUncertaintyMeters &&
             (!std::isfinite(*observation.rangeUncertaintyMeters) || *observation.rangeUncertaintyMeters < 0.0F)))
        {
            return std::unexpected("invalid or time-reversing SensorObservation");
        }

        const auto advanced = AdvanceTo(observation.observationTimeSeconds);
        if (!advanced)
        {
            return std::unexpected(advanced.error());
        }

        Record* best = nullptr;
        float bestDelta = config_.associationGateRadians;
        for (Record& record : records_)
        {
            if (record.track.lifecycle == TrackLifecycleState::Lost)
            {
                continue;
            }
            const float delta = std::abs(ShortestAngleDelta(record.track.estimatedBearingRadians,
                                                            observation.measuredBearingRadians));
            if (delta <= bestDelta)
            {
                bestDelta = delta;
                best = &record;
            }
        }

        if (best == nullptr)
        {
            if (records_.size() >= config_.maximumTracks)
            {
                return std::unexpected("TrackManager maximum track count reached");
            }
            records_.push_back(CreateRecord(observation));
            return records_.back().track.trackId;
        }

        UpdateRecord(*best, observation);
        return best->track.trackId;
    }

    [[nodiscard]] std::expected<void, std::string> AdvanceTo(const double simulationTimeSeconds)
    {
        if (!std::isfinite(simulationTimeSeconds) || simulationTimeSeconds < currentTimeSeconds_)
        {
            return std::unexpected("TrackManager time must be finite and monotonic");
        }
        currentTimeSeconds_ = simulationTimeSeconds;

        for (Record& record : records_)
        {
            const double ageSeconds = currentTimeSeconds_ - record.track.lastObservationTimeSeconds;
            if (ageSeconds >= config_.lostAfterSeconds)
            {
                record.track.lifecycle = TrackLifecycleState::Lost;
                record.track.confidence = 0.0F;
                record.contact.confidence = 0.0F;
                continue;
            }

            record.track.confidence = std::clamp(
                record.confidenceAtLastObservation - config_.confidenceDecayPerSecond * static_cast<float>(ageSeconds),
                0.0F, 1.0F);
            record.contact.confidence = record.track.confidence;
            record.track.bearingUncertaintyRadians = record.bearingUncertaintyAtLastObservation +
                config_.bearingUncertaintyGrowthRadiansPerSecond * static_cast<float>(ageSeconds);
            record.contact.bearingUncertaintyRadians = record.track.bearingUncertaintyRadians;

            if (record.positionUncertaintyAtEstimateMeters && record.positionEstimateTimeSeconds)
            {
                const double positionAgeSeconds = currentTimeSeconds_ - *record.positionEstimateTimeSeconds;
                record.track.positionUncertaintyMeters = *record.positionUncertaintyAtEstimateMeters +
                    config_.positionUncertaintyGrowthMetersPerSecond * static_cast<float>(positionAgeSeconds);
            }

            if (ageSeconds >= config_.coastAfterSeconds)
            {
                record.track.lifecycle = TrackLifecycleState::Coasting;
            }
            else
            {
                record.track.lifecycle = record.track.observationCount >= config_.observationsToConfirm
                    ? TrackLifecycleState::Confirmed
                    : TrackLifecycleState::Tentative;
            }
        }
        return {};
    }

    [[nodiscard]] std::vector<Contact> Contacts() const
    {
        std::vector<Contact> result;
        result.reserve(records_.size());
        for (const Record& record : records_)
        {
            result.push_back(record.contact);
        }
        return result;
    }

    [[nodiscard]] std::vector<Track> Tracks() const
    {
        std::vector<Track> result;
        result.reserve(records_.size());
        for (const Record& record : records_)
        {
            result.push_back(record.track);
        }
        return result;
    }

private:
    struct SpatialEstimate final
    {
        Physics::PhysicsVector3 positionMeters{};
        float uncertaintyMeters = 0.0F;
    };

    struct Record final
    {
        Contact contact{};
        Track track{};
        float confidenceAtLastObservation = 0.0F;
        float bearingUncertaintyAtLastObservation = 0.0F;
        std::optional<float> positionUncertaintyAtEstimateMeters{};
        std::optional<double> positionEstimateTimeSeconds{};
    };

    explicit TrackManager(TrackManagerConfig config)
        : config_(std::move(config))
    {
    }

    [[nodiscard]] static float WrapAngle(const float radians) noexcept
    {
        return std::remainder(radians, 6.2831853F);
    }

    [[nodiscard]] static float ShortestAngleDelta(const float from, const float to) noexcept
    {
        return WrapAngle(to - from);
    }

    [[nodiscard]] static std::optional<SpatialEstimate> MakeSpatialEstimate(const SensorObservation& observation)
    {
        if (!observation.sensorPositionMeters || !observation.estimatedRangeMeters ||
            !observation.rangeUncertaintyMeters)
        {
            return std::nullopt;
        }

        const float bearing = WrapAngle(observation.measuredBearingRadians);
        const float rangeMeters = *observation.estimatedRangeMeters;
        const float lateralUncertaintyMeters = std::abs(rangeMeters * observation.bearingUncertaintyRadians);
        const float uncertaintyMeters = static_cast<float>(
            std::hypot(static_cast<double>(*observation.rangeUncertaintyMeters),
                       static_cast<double>(lateralUncertaintyMeters)));

        return SpatialEstimate{
            .positionMeters = {
                .x = observation.sensorPositionMeters->x + std::cos(bearing) * rangeMeters,
                .y = observation.sensorPositionMeters->y + std::sin(bearing) * rangeMeters,
                .z = observation.sensorPositionMeters->z},
            .uncertaintyMeters = uncertaintyMeters};
    }

    [[nodiscard]] Record CreateRecord(const SensorObservation& observation)
    {
        const std::uint64_t contactId = nextContactId_++;
        const std::uint64_t trackId = nextTrackId_++;
        Record record{
            .contact = {
                .contactId = contactId,
                .classification = ContactClassification::Unknown,
                .lastMeasuredBearingRadians = WrapAngle(observation.measuredBearingRadians),
                .bearingUncertaintyRadians = observation.bearingUncertaintyRadians,
                .confidence = observation.confidence,
                .observationCount = 1U,
                .firstObservationTimeSeconds = observation.observationTimeSeconds,
                .lastObservationTimeSeconds = observation.observationTimeSeconds},
            .track = {
                .trackId = trackId,
                .contactId = contactId,
                .lifecycle = config_.observationsToConfirm <= 1U ? TrackLifecycleState::Confirmed : TrackLifecycleState::Tentative,
                .estimatedPositionMeters = std::nullopt,
                .positionUncertaintyMeters = std::nullopt,
                .estimatedVelocityMetersPerSecond = std::nullopt,
                .estimatedBearingRadians = WrapAngle(observation.measuredBearingRadians),
                .bearingUncertaintyRadians = observation.bearingUncertaintyRadians,
                .confidence = observation.confidence,
                .observationCount = 1U,
                .firstObservationTimeSeconds = observation.observationTimeSeconds,
                .lastObservationTimeSeconds = observation.observationTimeSeconds},
            .confidenceAtLastObservation = observation.confidence,
            .bearingUncertaintyAtLastObservation = observation.bearingUncertaintyRadians,
            .positionUncertaintyAtEstimateMeters = std::nullopt,
            .positionEstimateTimeSeconds = std::nullopt};

        if (const auto spatial = MakeSpatialEstimate(observation))
        {
            record.track.estimatedPositionMeters = spatial->positionMeters;
            record.track.positionUncertaintyMeters = spatial->uncertaintyMeters;
            record.positionUncertaintyAtEstimateMeters = spatial->uncertaintyMeters;
            record.positionEstimateTimeSeconds = observation.observationTimeSeconds;
        }
        return record;
    }

    void UpdateRecord(Record& record, const SensorObservation& observation)
    {
        const float bearingDelta = ShortestAngleDelta(record.track.estimatedBearingRadians,
                                                      observation.measuredBearingRadians);
        const float fusedBearing = WrapAngle(record.track.estimatedBearingRadians + 0.5F * bearingDelta);
        const float fusedUncertainty = std::min(record.bearingUncertaintyAtLastObservation,
                                                observation.bearingUncertaintyRadians);
        const float fusedConfidence = std::clamp(
            std::max(record.confidenceAtLastObservation, observation.confidence) + 0.08F, 0.0F, 1.0F);

        ++record.contact.observationCount;
        record.contact.lastMeasuredBearingRadians = WrapAngle(observation.measuredBearingRadians);
        record.contact.bearingUncertaintyRadians = fusedUncertainty;
        record.contact.confidence = fusedConfidence;
        record.contact.lastObservationTimeSeconds = observation.observationTimeSeconds;

        ++record.track.observationCount;
        record.track.estimatedBearingRadians = fusedBearing;
        record.track.bearingUncertaintyRadians = fusedUncertainty;
        record.track.confidence = fusedConfidence;
        record.track.lastObservationTimeSeconds = observation.observationTimeSeconds;
        record.track.lifecycle = record.track.observationCount >= config_.observationsToConfirm
            ? TrackLifecycleState::Confirmed
            : TrackLifecycleState::Tentative;

        if (const auto spatial = MakeSpatialEstimate(observation))
        {
            record.track.estimatedPositionMeters = spatial->positionMeters;
            record.track.positionUncertaintyMeters = spatial->uncertaintyMeters;
            record.positionUncertaintyAtEstimateMeters = spatial->uncertaintyMeters;
            record.positionEstimateTimeSeconds = observation.observationTimeSeconds;
        }

        record.confidenceAtLastObservation = fusedConfidence;
        record.bearingUncertaintyAtLastObservation = fusedUncertainty;
    }

    TrackManagerConfig config_{};
    std::vector<Record> records_;
    std::uint64_t nextContactId_ = 1;
    std::uint64_t nextTrackId_ = 1;
    double currentTimeSeconds_ = 0.0;
};
}
