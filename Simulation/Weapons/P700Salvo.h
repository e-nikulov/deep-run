#pragma once

#include "Simulation/Weapons/P700Granit.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace DeepRun::Weapons
{
enum class P700SalvoMode : std::uint8_t
{
    Single = 1U,
    Pair = 2U,
};

struct P700SalvoObservation final
{
    std::uint64_t missileId = 0U;
    std::uint64_t trackId = 0U;
    Physics::PhysicsVector3 perceivedAimPointMeters{};
    float positionUncertaintyMeters = 0.0F;
    float estimatedBearingRadians = 0.0F;
    float bearingUncertaintyRadians = 0.0F;
    float confidence = 0.0F;
};

struct P700SalvoTrack final
{
    std::uint64_t trackId = 0U;
    Physics::PhysicsVector3 perceivedAimPointMeters{};
    float positionUncertaintyMeters = 0.0F;
    float estimatedBearingRadians = 0.0F;
    float bearingUncertaintyRadians = 0.0F;
    float confidence = 0.0F;
    std::size_t contributingMissiles = 0U;
};

[[nodiscard]] inline float P700SalvoObservationDistance(
    const Physics::PhysicsVector3& first,
    const Physics::PhysicsVector3& second) noexcept
{
    const double dx = static_cast<double>(second.x) - first.x;
    const double dy = static_cast<double>(second.y) - first.y;
    const double dz = static_cast<double>(second.z) - first.z;
    return static_cast<float>(std::sqrt(dx * dx + dy * dy + dz * dz));
}

[[nodiscard]] inline std::expected<P700SalvoTrack, std::string> FuseP700SalvoObservations(
    const std::span<const P700SalvoObservation> observations)
{
    if (observations.empty())
    {
        return std::unexpected("P-700 salvo fusion requires at least one perceived observation");
    }

    for (const auto& observation : observations)
    {
        if (observation.missileId == 0U || observation.trackId == 0U ||
            !observation.perceivedAimPointMeters.IsFinite() ||
            !std::isfinite(observation.positionUncertaintyMeters) || observation.positionUncertaintyMeters <= 0.0F ||
            !std::isfinite(observation.estimatedBearingRadians) ||
            !std::isfinite(observation.bearingUncertaintyRadians) || observation.bearingUncertaintyRadians <= 0.0F ||
            !std::isfinite(observation.confidence) || observation.confidence < 0.0F || observation.confidence > 1.0F)
        {
            return std::unexpected("P-700 salvo fusion received invalid perceived evidence");
        }
        if (observation.trackId != observations.front().trackId)
        {
            return std::unexpected("P-700 salvo members may fuse only the same perceived Track identity");
        }
    }

    const auto anchorIt = std::min_element(
        observations.begin(), observations.end(), [](const auto& left, const auto& right) {
            if (left.positionUncertaintyMeters != right.positionUncertaintyMeters)
                return left.positionUncertaintyMeters < right.positionUncertaintyMeters;
            return left.confidence > right.confidence;
        });
    const P700SalvoObservation& anchor = *anchorIt;

    double totalPositionWeight = 0.0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double independentPositionInformation = 0.0;
    double sinBearing = 0.0;
    double cosBearing = 0.0;
    double bearingWeightTotal = 0.0;
    double independentBearingInformation = 0.0;
    float bestPositionUncertainty = anchor.positionUncertaintyMeters;
    float bestBearingUncertainty = anchor.bearingUncertaintyRadians;
    float bestConfidence = anchor.confidence;
    std::size_t contributors = 0U;

    for (const auto& observation : observations)
    {
        // Missile observations that disagree by more than their combined error budget are not averaged into a
        // fictitiously precise solution. They remain evidence of ambiguity instead of leaking target truth.
        const float separation = P700SalvoObservationDistance(
            anchor.perceivedAimPointMeters, observation.perceivedAimPointMeters);
        const float consistencyGateMeters = std::max(
            250.0F,
            2.5F * std::hypot(anchor.positionUncertaintyMeters, observation.positionUncertaintyMeters));
        if (!std::isfinite(separation) || separation > consistencyGateMeters)
        {
            continue;
        }

        const double safePositionUncertainty = std::max(25.0, static_cast<double>(observation.positionUncertaintyMeters));
        const double confidenceWeight = std::max(0.05, static_cast<double>(observation.confidence));
        const double positionWeight = confidenceWeight / (safePositionUncertainty * safePositionUncertainty);
        totalPositionWeight += positionWeight;
        x += static_cast<double>(observation.perceivedAimPointMeters.x) * positionWeight;
        y += static_cast<double>(observation.perceivedAimPointMeters.y) * positionWeight;
        z += static_cast<double>(observation.perceivedAimPointMeters.z) * positionWeight;
        independentPositionInformation += 1.0 / (safePositionUncertainty * safePositionUncertainty);

        const double safeBearingUncertainty = std::max(0.0025, static_cast<double>(observation.bearingUncertaintyRadians));
        const double bearingWeight = confidenceWeight / (safeBearingUncertainty * safeBearingUncertainty);
        sinBearing += std::sin(static_cast<double>(observation.estimatedBearingRadians)) * bearingWeight;
        cosBearing += std::cos(static_cast<double>(observation.estimatedBearingRadians)) * bearingWeight;
        bearingWeightTotal += bearingWeight;
        independentBearingInformation += 1.0 / (safeBearingUncertainty * safeBearingUncertainty);

        bestPositionUncertainty = std::min(bestPositionUncertainty, observation.positionUncertaintyMeters);
        bestBearingUncertainty = std::min(bestBearingUncertainty, observation.bearingUncertaintyRadians);
        bestConfidence = std::max(bestConfidence, observation.confidence);
        ++contributors;
    }

    if (contributors == 0U || totalPositionWeight <= 0.0 || bearingWeightTotal <= 0.0 ||
        independentPositionInformation <= 0.0 || independentBearingInformation <= 0.0)
    {
        return std::unexpected("P-700 salvo fusion found no mutually consistent perceived observations");
    }

    const float independentPositionUncertainty = static_cast<float>(1.0 / std::sqrt(independentPositionInformation));
    const float independentBearingUncertainty = static_cast<float>(1.0 / std::sqrt(independentBearingInformation));

    // Common environment/seeker errors are correlated, so extra missiles have diminishing returns and can never
    // drive uncertainty toward zero merely by increasing salvo size. This is explicit GAME POLICY.
    constexpr float CorrelatedUncertaintyFloorFraction = 0.60F;
    const float fusedPositionUncertainty = std::max(
        independentPositionUncertainty,
        bestPositionUncertainty * CorrelatedUncertaintyFloorFraction);
    const float fusedBearingUncertainty = std::max(
        independentBearingUncertainty,
        bestBearingUncertainty * CorrelatedUncertaintyFloorFraction);
    const float confidenceGain = contributors > 1U
        ? static_cast<float>(1.0 - std::exp(-0.35 * static_cast<double>(contributors - 1U)))
        : 0.0F;
    const float fusedConfidence = std::clamp(
        bestConfidence + (1.0F - bestConfidence) * confidenceGain,
        0.0F,
        1.0F);

    return P700SalvoTrack{
        .trackId = observations.front().trackId,
        .perceivedAimPointMeters = {
            .x = static_cast<float>(x / totalPositionWeight),
            .y = static_cast<float>(y / totalPositionWeight),
            .z = static_cast<float>(z / totalPositionWeight)},
        .positionUncertaintyMeters = fusedPositionUncertainty,
        .estimatedBearingRadians = static_cast<float>(std::atan2(sinBearing, cosBearing)),
        .bearingUncertaintyRadians = fusedBearingUncertainty,
        .confidence = fusedConfidence,
        .contributingMissiles = contributors};
}

struct P700SalvoRuntimeState final
{
    std::uint64_t salvoId = 0U;
    std::uint64_t guidanceTrackId = 0U;
    std::vector<P700GranitRuntimeState> missiles{};
    std::optional<P700SalvoTrack> fusedTrack{};
};

[[nodiscard]] inline std::expected<P700SalvoRuntimeState, std::string> LaunchP700Salvo(
    const P700GranitDefinition& definition,
    const Perception::Track& targetTrack,
    const std::span<const P700CarrierLaunchContext> launchContexts,
    const double simulationTimeSeconds,
    const std::uint64_t salvoId)
{
    if (salvoId == 0U || launchContexts.empty() || launchContexts.size() > 4U)
    {
        return std::unexpected("P-700 salvo requires one to four launch contexts and a non-zero salvo ID");
    }

    P700SalvoRuntimeState salvo{
        .salvoId = salvoId,
        .guidanceTrackId = targetTrack.trackId};
    salvo.missiles.reserve(launchContexts.size());

    for (std::size_t index = 0U; index < launchContexts.size(); ++index)
    {
        auto missile = CreateP700GranitRuntime(definition, simulationTimeSeconds);
        if (!missile)
        {
            return std::unexpected("P-700 salvo member creation failed: " + missile.error());
        }
        const auto launched = LaunchP700Granit(
            definition, *missile, targetTrack, launchContexts[index], simulationTimeSeconds);
        if (!launched)
        {
            return std::unexpected("P-700 salvo member launch failed: " + launched.error());
        }
        if (!launched->allowed)
        {
            return std::unexpected("P-700 salvo member employment was rejected: " + launched->reason);
        }
        missile->terminalRandomSeed = P700SplitMix64(
            missile->terminalRandomSeed ^ salvoId ^ static_cast<std::uint64_t>(index + 1U));
        salvo.missiles.push_back(std::move(*missile));
    }
    return salvo;
}

[[nodiscard]] inline Perception::Track P700SalvoTrackAsPerceivedTrack(
    const P700SalvoTrack& salvoTrack,
    const double simulationTimeSeconds)
{
    return Perception::Track{
        .trackId = salvoTrack.trackId,
        .contactId = salvoTrack.trackId,
        .lifecycle = Perception::TrackLifecycleState::Confirmed,
        .estimatedPositionMeters = salvoTrack.perceivedAimPointMeters,
        .positionUncertaintyMeters = salvoTrack.positionUncertaintyMeters,
        .estimatedVelocityMetersPerSecond = std::nullopt,
        .estimatedBearingRadians = salvoTrack.estimatedBearingRadians,
        .bearingUncertaintyRadians = salvoTrack.bearingUncertaintyRadians,
        .confidence = salvoTrack.confidence,
        .observationCount = salvoTrack.contributingMissiles,
        .firstObservationTimeSeconds = simulationTimeSeconds,
        .lastObservationTimeSeconds = simulationTimeSeconds,
        .classification = Perception::ContactClassification::Unknown,
        .visuallyIdentified = false,
        .opticalIdentificationLevel = Perception::OpticalIdentificationLevel::None};
}

[[nodiscard]] inline std::expected<void, std::string> UpdateP700SalvoGuidance(
    const P700GranitDefinition& definition,
    P700SalvoRuntimeState& salvo,
    const std::span<const P700SalvoObservation> observations,
    const double simulationTimeSeconds)
{
    if (salvo.salvoId == 0U || salvo.guidanceTrackId == 0U || salvo.missiles.empty() ||
        !std::isfinite(simulationTimeSeconds) || simulationTimeSeconds < 0.0)
    {
        return std::unexpected("P-700 salvo guidance state is invalid");
    }
    if (observations.empty())
    {
        return {};
    }

    const auto fused = FuseP700SalvoObservations(observations);
    if (!fused)
    {
        return std::unexpected(fused.error());
    }
    if (fused->trackId != salvo.guidanceTrackId)
    {
        return std::unexpected("P-700 salvo fusion changed the launch Track identity");
    }

    const Perception::Track perceived = P700SalvoTrackAsPerceivedTrack(*fused, simulationTimeSeconds);
    const auto valid = ValidateTrackForWeapon(definition.weapon, perceived);
    if (!valid)
    {
        // Poor cooperative evidence is allowed to exist, but it must not overwrite the last qualified guidance.
        return {};
    }
    for (auto& missile : salvo.missiles)
    {
        const auto updated = UpdateP700PerceivedGuidance(definition, missile, perceived);
        if (!updated)
        {
            return std::unexpected("P-700 salvo guidance propagation failed: " + updated.error());
        }
    }
    salvo.fusedTrack = *fused;
    return {};
}

[[nodiscard]] inline std::expected<std::vector<P700GranitImpact>, std::string> AdvanceP700SalvoWithCollision(
    const P700GranitDefinition& definition,
    P700SalvoRuntimeState& salvo,
    const std::optional<Perception::Track>& latestCarrierTrack,
    const std::span<const P700SalvoObservation> cooperativeObservations,
    Physics::PhysicsWorld& physicsWorld,
    const double simulationTimeSeconds,
    const Physics::PhysicsBodyHandle ignoredCarrierBody = {},
    const std::optional<P700TerminalDefenseProfile>& targetDefense = std::nullopt)
{
    if (!cooperativeObservations.empty())
    {
        const auto updated = UpdateP700SalvoGuidance(
            definition, salvo, cooperativeObservations, simulationTimeSeconds);
        if (!updated)
        {
            return std::unexpected(updated.error());
        }
    }

    std::optional<Perception::Track> guidanceTrack = latestCarrierTrack;
    if (salvo.fusedTrack)
    {
        guidanceTrack = P700SalvoTrackAsPerceivedTrack(*salvo.fusedTrack, simulationTimeSeconds);
    }
    if (guidanceTrack && guidanceTrack->trackId != salvo.guidanceTrackId)
    {
        return std::unexpected("P-700 salvo advance received a different perceived Track identity");
    }

    std::vector<P700GranitImpact> impacts;
    for (auto& missile : salvo.missiles)
    {
        if (missile.phase == P700GranitPhase::Spent)
        {
            continue;
        }
        const auto advanced = AdvanceP700GranitWithCollision(
            definition,
            missile,
            guidanceTrack,
            physicsWorld,
            simulationTimeSeconds,
            ignoredCarrierBody,
            targetDefense);
        if (!advanced)
        {
            return std::unexpected("P-700 salvo member advance failed: " + advanced.error());
        }
        if (advanced->has_value())
        {
            impacts.push_back(**advanced);
        }
    }
    return impacts;
}
} // namespace DeepRun::Weapons
