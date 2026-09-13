#pragma once

#include "Simulation/Perception/TrackManager.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <span>
#include <string>

namespace DeepRun::Game::Combat
{
enum class ContactKnowledgeLevel
{
    BearingOnly,
    AreaEstimate,
    Classified,
    PositiveIdentification,
    Coasting,
};

struct ContactHypothesis final
{
    std::uint64_t trackId = 0;
    ContactKnowledgeLevel knowledge = ContactKnowledgeLevel::BearingOnly;
    Perception::TrackLifecycleState lifecycle = Perception::TrackLifecycleState::Tentative;
    float confidence = 0.0F;
    float bearingRadians = 0.0F;
    float bearingUncertaintyRadians = 0.0F;
    std::optional<Physics::PhysicsVector3> estimatedPositionMeters{};
    std::optional<float> estimatedRangeMeters{};
    std::optional<float> hypothesisRadiusMeters{};
    Perception::ContactClassification classification = Perception::ContactClassification::Unknown;
    Perception::OpticalIdentificationLevel opticalIdentificationLevel =
        Perception::OpticalIdentificationLevel::None;
};

[[nodiscard]] inline std::expected<ContactHypothesis, std::string> BuildContactHypothesis(
    const Perception::Track& track,
    const Physics::PhysicsVector3& observerPositionMeters)
{
    if (track.trackId == 0U || track.lifecycle == Perception::TrackLifecycleState::Lost ||
        !observerPositionMeters.IsFinite() || !std::isfinite(track.confidence) || track.confidence < 0.0F ||
        track.confidence > 1.0F || !std::isfinite(track.estimatedBearingRadians) ||
        !std::isfinite(track.bearingUncertaintyRadians) || track.bearingUncertaintyRadians < 0.0F ||
        (track.estimatedPositionMeters && !track.estimatedPositionMeters->IsFinite()) ||
        (track.positionUncertaintyMeters &&
         (!std::isfinite(*track.positionUncertaintyMeters) || *track.positionUncertaintyMeters < 0.0F)))
    {
        return std::unexpected("invalid perceived Track for combat-knowledge projection");
    }

    ContactKnowledgeLevel level = ContactKnowledgeLevel::BearingOnly;
    if (track.lifecycle == Perception::TrackLifecycleState::Coasting)
    {
        level = ContactKnowledgeLevel::Coasting;
    }
    else if (track.estimatedPositionMeters)
    {
        level = ContactKnowledgeLevel::AreaEstimate;
    }
    if (track.classification != Perception::ContactClassification::Unknown &&
        static_cast<int>(track.opticalIdentificationLevel) >=
            static_cast<int>(Perception::OpticalIdentificationLevel::TypeResolved))
    {
        level = ContactKnowledgeLevel::Classified;
    }
    if (track.classification != Perception::ContactClassification::Unknown &&
        track.opticalIdentificationLevel == Perception::OpticalIdentificationLevel::FlagOrMarkingsResolved)
    {
        level = ContactKnowledgeLevel::PositiveIdentification;
    }

    ContactHypothesis result{
        .trackId = track.trackId,
        .knowledge = level,
        .lifecycle = track.lifecycle,
        .confidence = track.confidence,
        .bearingRadians = track.estimatedBearingRadians,
        .bearingUncertaintyRadians = track.bearingUncertaintyRadians,
        .estimatedPositionMeters = track.estimatedPositionMeters,
        .hypothesisRadiusMeters = track.positionUncertaintyMeters,
        .classification = track.classification,
        .opticalIdentificationLevel = track.opticalIdentificationLevel};

    if (track.estimatedPositionMeters)
    {
        const double dx = static_cast<double>(track.estimatedPositionMeters->x) - observerPositionMeters.x;
        const double dy = static_cast<double>(track.estimatedPositionMeters->y) - observerPositionMeters.y;
        const double dz = static_cast<double>(track.estimatedPositionMeters->z) - observerPositionMeters.z;
        const double range = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (!std::isfinite(range) || range > static_cast<double>(std::numeric_limits<float>::max()))
        {
            return std::unexpected("perceived Track range is not representable");
        }
        result.estimatedRangeMeters = static_cast<float>(range);
    }
    return result;
}

enum class HostileAwarenessLevel
{
    Unaware,
    Suspected,
    Localized,
    FireControlQuality,
};

struct HostileAwarenessConfig final
{
    float suspectedMinimumConfidence = 0.35F;
    float suspectedMaximumBearingUncertaintyRadians = 0.30F;
    float localizedMinimumConfidence = 0.55F;
    float localizedMaximumPositionUncertaintyMeters = 1'000.0F;
    float fireControlMinimumConfidence = 0.70F;
    float fireControlMaximumBearingUncertaintyRadians = 0.10F;
    float fireControlMaximumPositionUncertaintyMeters = 150.0F;
};

[[nodiscard]] inline HostileAwarenessLevel AssessHiddenHostileAwareness(
    const std::span<const Perception::Track> hostileTracks,
    const HostileAwarenessConfig& config = {}) noexcept
{
    HostileAwarenessLevel best = HostileAwarenessLevel::Unaware;
    for (const auto& track : hostileTracks)
    {
        if (track.lifecycle == Perception::TrackLifecycleState::Lost || !std::isfinite(track.confidence) ||
            !std::isfinite(track.bearingUncertaintyRadians))
        {
            continue;
        }
        if (track.confidence >= config.suspectedMinimumConfidence &&
            track.bearingUncertaintyRadians <= config.suspectedMaximumBearingUncertaintyRadians)
        {
            best = std::max(best, HostileAwarenessLevel::Suspected);
        }
        if (track.estimatedPositionMeters && track.positionUncertaintyMeters &&
            track.confidence >= config.localizedMinimumConfidence &&
            *track.positionUncertaintyMeters <= config.localizedMaximumPositionUncertaintyMeters)
        {
            best = std::max(best, HostileAwarenessLevel::Localized);
        }
        if (track.estimatedPositionMeters && track.positionUncertaintyMeters &&
            track.confidence >= config.fireControlMinimumConfidence &&
            track.bearingUncertaintyRadians <= config.fireControlMaximumBearingUncertaintyRadians &&
            *track.positionUncertaintyMeters <= config.fireControlMaximumPositionUncertaintyMeters &&
            track.lifecycle != Perception::TrackLifecycleState::Coasting)
        {
            best = std::max(best, HostileAwarenessLevel::FireControlQuality);
        }
    }
    return best;
}

enum class ExposureSource
{
    ActiveSonarTransmission,
    RaisedPeriscopeMast,
    TorpedoLaunch,
    P700Launch,
};

struct ExposurePolicy final
{
    float maximumDetectionRangeMeters = 1.0F;
    float nearDetectionProbability = 0.0F;
    float minimumConfidence = 0.0F;
    float maximumConfidence = 0.0F;
    float minimumBearingUncertaintyRadians = 0.0F;
    float maximumBearingUncertaintyRadians = 0.0F;
};

[[nodiscard]] constexpr ExposurePolicy ExposurePolicyFor(const ExposureSource source) noexcept
{
    // Explicit GAME POLICY. These values express relative gameplay exposure, not real sensor TTX.
    switch (source)
    {
    case ExposureSource::ActiveSonarTransmission:
        return {120'000.0F, 0.98F, 0.45F, 0.95F, 0.035F, 0.16F};
    case ExposureSource::RaisedPeriscopeMast:
        return {24'000.0F, 0.82F, 0.35F, 0.72F, 0.050F, 0.20F};
    case ExposureSource::TorpedoLaunch:
        return {35'000.0F, 0.68F, 0.30F, 0.62F, 0.080F, 0.26F};
    case ExposureSource::P700Launch:
        return {160'000.0F, 0.96F, 0.48F, 0.92F, 0.040F, 0.15F};
    }
    return {};
}

[[nodiscard]] inline float ExposureDetectionProbability(
    const ExposureSource source,
    const float rangeMeters) noexcept
{
    const ExposurePolicy policy = ExposurePolicyFor(source);
    if (!std::isfinite(rangeMeters) || rangeMeters < 0.0F || policy.maximumDetectionRangeMeters <= 0.0F ||
        rangeMeters >= policy.maximumDetectionRangeMeters)
    {
        return 0.0F;
    }
    const float normalized = std::clamp(rangeMeters / policy.maximumDetectionRangeMeters, 0.0F, 1.0F);
    const float propagationQuality = (1.0F - normalized) * (1.0F - normalized);
    return std::clamp(policy.nearDetectionProbability * propagationQuality, 0.0F, 1.0F);
}

[[nodiscard]] inline std::uint64_t CombatKnowledgeSplitMix64(std::uint64_t value) noexcept
{
    value += 0x9E3779B97F4A7C15ULL;
    value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31U);
}

[[nodiscard]] inline float CombatKnowledgeUnitRandom(const std::uint64_t seed) noexcept
{
    const std::uint64_t bits = CombatKnowledgeSplitMix64(seed);
    return static_cast<float>((bits >> 40U) * (1.0 / 16777216.0));
}

[[nodiscard]] inline std::expected<std::optional<Perception::SensorObservation>, std::string> ObserveExposureEvent(
    const ExposureSource source,
    const Physics::PhysicsVector3& observerPositionMeters,
    const Physics::PhysicsVector3& exposedPositionMeters,
    const std::string& observerSensorId,
    const double simulationTimeSeconds,
    const std::uint64_t deterministicSeed)
{
    if (!observerPositionMeters.IsFinite() || !exposedPositionMeters.IsFinite() || observerSensorId.empty() ||
        !std::isfinite(simulationTimeSeconds) || simulationTimeSeconds < 0.0)
    {
        return std::unexpected("invalid exposure observation input");
    }

    const double dx = static_cast<double>(exposedPositionMeters.x) - observerPositionMeters.x;
    const double dy = static_cast<double>(exposedPositionMeters.y) - observerPositionMeters.y;
    const double dz = static_cast<double>(exposedPositionMeters.z) - observerPositionMeters.z;
    const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (!std::isfinite(distance) || distance > static_cast<double>(std::numeric_limits<float>::max()))
    {
        return std::unexpected("exposure observation distance is invalid");
    }

    const float rangeMeters = static_cast<float>(distance);
    const float probability = ExposureDetectionProbability(source, rangeMeters);
    if (probability <= 0.0F || CombatKnowledgeUnitRandom(deterministicSeed) >= probability)
    {
        return std::optional<Perception::SensorObservation>{};
    }

    const ExposurePolicy policy = ExposurePolicyFor(source);
    const float normalizedRange = std::clamp(rangeMeters / policy.maximumDetectionRangeMeters, 0.0F, 1.0F);
    const float confidence = std::lerp(policy.maximumConfidence, policy.minimumConfidence, normalizedRange);
    const float bearingUncertainty = std::lerp(
        policy.minimumBearingUncertaintyRadians,
        policy.maximumBearingUncertaintyRadians,
        normalizedRange);
    const bool optical = source == ExposureSource::RaisedPeriscopeMast;

    return std::optional<Perception::SensorObservation>{Perception::SensorObservation{
        .modality = optical ? Perception::SensorModality::Optical : Perception::SensorModality::PassiveAcoustic,
        .sensorId = observerSensorId,
        .sensorPositionMeters = observerPositionMeters,
        .observationTimeSeconds = simulationTimeSeconds,
        .measuredBearingRadians = static_cast<float>(std::atan2(dy, dx)),
        .bearingUncertaintyRadians = bearingUncertainty,
        .estimatedRangeMeters = std::nullopt,
        .rangeUncertaintyMeters = std::nullopt,
        .confidence = confidence,
        .opticalIdentificationLevel = optical
            ? Perception::OpticalIdentificationLevel::Detected
            : Perception::OpticalIdentificationLevel::None,
        .classificationEvidence = std::nullopt}};
}
} // namespace DeepRun::Game::Combat
