#pragma once

#include "Simulation/Acoustics/ActiveSonar.h"
#include "Simulation/Perception/TrackManager.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace DeepRun::Game::Combat
{
inline constexpr float M5SonarScopeMinimumRangeMeters = 2'000.0F;
inline constexpr float M5SonarScopeMaximumRangeMeters = 50'000.0F;
inline constexpr double M5SonarEchoPersistenceSeconds = 4.0;

struct SonarTrackPresentation final
{
    std::uint64_t trackId = 0;
    Perception::TrackLifecycleState lifecycle = Perception::TrackLifecycleState::Tentative;
    float relativeBearingRadians = 0.0F;
    float bearingUncertaintyRadians = 0.0F;
    float confidence = 0.0F;
    std::optional<float> estimatedRangeMeters{};
    std::optional<float> positionUncertaintyMeters{};
    bool selected = false;
};

struct SonarActivePulsePresentation final
{
    float relativeBearingRadians = 0.0F;
    float beamHalfAngleRadians = 0.0F;
    double emissionTimeSeconds = 0.0;
};

struct SonarEchoPresentation final
{
    float relativeBearingRadians = 0.0F;
    float bearingUncertaintyRadians = 0.0F;
    float estimatedRangeMeters = 0.0F;
    float rangeUncertaintyMeters = 0.0F;
    float confidence = 0.0F;
    double observationTimeSeconds = 0.0;
};

struct SonarPresentationSnapshot final
{
    std::vector<SonarTrackPresentation> tracks{};
    std::optional<SonarActivePulsePresentation> activePulse{};
    std::optional<SonarEchoPresentation> recentEcho{};
    float displayRangeMeters = M5SonarScopeMinimumRangeMeters;
    float ownshipHeadingRadians = 0.0F;
    double simulationTimeSeconds = 0.0;
};

[[nodiscard]] inline float WrapSonarAngle(const float angleRadians) noexcept
{
    constexpr float pi = 3.14159265358979323846F;
    constexpr float twoPi = 2.0F * pi;
    float wrapped = std::fmod(angleRadians + pi, twoPi);
    if (wrapped < 0.0F)
    {
        wrapped += twoPi;
    }
    return wrapped - pi;
}

// Presentation-only projection. Inputs are ownship state, perceived Tracks, the player's own transmitted pulse,
// and measured echo evidence. No hostile transform/body/entity identity is accepted by this boundary.
[[nodiscard]] inline std::expected<SonarPresentationSnapshot, std::string> BuildSonarPresentation(
    const std::span<const Perception::Track> tracks,
    const std::optional<std::uint64_t> selectedTrackId,
    const Physics::PhysicsVector3& ownshipPositionMeters,
    const float ownshipHeadingRadians,
    const std::optional<Acoustics::ActiveAcousticPulse>& activePulse,
    const std::optional<Acoustics::AcousticObservation>& recentActiveEcho,
    const double simulationTimeSeconds)
{
    if (!ownshipPositionMeters.IsFinite() || !std::isfinite(ownshipHeadingRadians) ||
        !std::isfinite(simulationTimeSeconds) || simulationTimeSeconds < 0.0)
    {
        return std::unexpected("M5 sonar presentation ownship/time input is invalid");
    }

    SonarPresentationSnapshot result{
        .ownshipHeadingRadians = ownshipHeadingRadians,
        .simulationTimeSeconds = simulationTimeSeconds};
    result.tracks.reserve(tracks.size());

    float requestedRangeMeters = M5SonarScopeMinimumRangeMeters;
    for (const auto& track : tracks)
    {
        if (track.lifecycle == Perception::TrackLifecycleState::Lost)
        {
            continue;
        }
        if (track.trackId == 0U || !std::isfinite(track.estimatedBearingRadians) ||
            !std::isfinite(track.bearingUncertaintyRadians) || track.bearingUncertaintyRadians < 0.0F ||
            !std::isfinite(track.confidence) || track.confidence < 0.0F || track.confidence > 1.0F)
        {
            return std::unexpected("M5 sonar presentation received an invalid perceived Track");
        }

        SonarTrackPresentation contact{
            .trackId = track.trackId,
            .lifecycle = track.lifecycle,
            .relativeBearingRadians = WrapSonarAngle(track.estimatedBearingRadians - ownshipHeadingRadians),
            .bearingUncertaintyRadians = track.bearingUncertaintyRadians,
            .confidence = track.confidence,
            .positionUncertaintyMeters = track.positionUncertaintyMeters,
            .selected = selectedTrackId.has_value() && *selectedTrackId == track.trackId};

        if (track.positionUncertaintyMeters &&
            (!std::isfinite(*track.positionUncertaintyMeters) || *track.positionUncertaintyMeters < 0.0F))
        {
            return std::unexpected("M5 sonar presentation received invalid perceived position uncertainty");
        }
        if (track.estimatedPositionMeters)
        {
            if (!track.estimatedPositionMeters->IsFinite())
            {
                return std::unexpected("M5 sonar presentation received an invalid perceived position");
            }
            const double dx = static_cast<double>(track.estimatedPositionMeters->x) - ownshipPositionMeters.x;
            const double dy = static_cast<double>(track.estimatedPositionMeters->y) - ownshipPositionMeters.y;
            const double dz = static_cast<double>(track.estimatedPositionMeters->z) - ownshipPositionMeters.z;
            const double rangeMeters = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (!std::isfinite(rangeMeters))
            {
                return std::unexpected("M5 sonar presentation perceived range is non-finite");
            }
            contact.estimatedRangeMeters = static_cast<float>(rangeMeters);
            const float uncertainty = track.positionUncertaintyMeters.value_or(0.0F);
            requestedRangeMeters = std::max(
                requestedRangeMeters,
                static_cast<float>(rangeMeters) * 1.20F + uncertainty);
        }
        result.tracks.push_back(contact);
    }

    if (activePulse)
    {
        if (!activePulse->forwardUnitVector.IsFinite() || !std::isfinite(activePulse->beamHalfAngleRadians) ||
            activePulse->beamHalfAngleRadians <= 0.0F || !std::isfinite(activePulse->emissionTimeSeconds) ||
            activePulse->emissionTimeSeconds < 0.0 || activePulse->emissionTimeSeconds > simulationTimeSeconds + 1.0e-6)
        {
            return std::unexpected("M5 sonar presentation received an invalid own active pulse");
        }
        const float absoluteBearing = static_cast<float>(std::atan2(
            static_cast<double>(activePulse->forwardUnitVector.y),
            static_cast<double>(activePulse->forwardUnitVector.x)));
        result.activePulse = SonarActivePulsePresentation{
            .relativeBearingRadians = WrapSonarAngle(absoluteBearing - ownshipHeadingRadians),
            .beamHalfAngleRadians = activePulse->beamHalfAngleRadians,
            .emissionTimeSeconds = activePulse->emissionTimeSeconds};
    }

    if (recentActiveEcho && recentActiveEcho->kind == Acoustics::AcousticObservationKind::ActiveEcho)
    {
        const double ageSeconds = simulationTimeSeconds - recentActiveEcho->observationTimeSeconds;
        if (ageSeconds >= -1.0e-6 && ageSeconds <= M5SonarEchoPersistenceSeconds &&
            recentActiveEcho->estimatedRangeMeters && recentActiveEcho->rangeUncertaintyMeters)
        {
            if (!std::isfinite(recentActiveEcho->measuredBearingRadians) ||
                !std::isfinite(recentActiveEcho->bearingUncertaintyRadians) ||
                recentActiveEcho->bearingUncertaintyRadians < 0.0F ||
                !std::isfinite(*recentActiveEcho->estimatedRangeMeters) || *recentActiveEcho->estimatedRangeMeters < 0.0F ||
                !std::isfinite(*recentActiveEcho->rangeUncertaintyMeters) || *recentActiveEcho->rangeUncertaintyMeters < 0.0F ||
                !std::isfinite(recentActiveEcho->confidence) || recentActiveEcho->confidence < 0.0F ||
                recentActiveEcho->confidence > 1.0F)
            {
                return std::unexpected("M5 sonar presentation received invalid active echo evidence");
            }
            result.recentEcho = SonarEchoPresentation{
                .relativeBearingRadians = WrapSonarAngle(
                    recentActiveEcho->measuredBearingRadians - ownshipHeadingRadians),
                .bearingUncertaintyRadians = recentActiveEcho->bearingUncertaintyRadians,
                .estimatedRangeMeters = *recentActiveEcho->estimatedRangeMeters,
                .rangeUncertaintyMeters = *recentActiveEcho->rangeUncertaintyMeters,
                .confidence = recentActiveEcho->confidence,
                .observationTimeSeconds = recentActiveEcho->observationTimeSeconds};
            requestedRangeMeters = std::max(
                requestedRangeMeters,
                recentActiveEcho->estimatedRangeMeters.value() * 1.20F + recentActiveEcho->rangeUncertaintyMeters.value());
        }
    }

    result.displayRangeMeters = std::clamp(
        requestedRangeMeters, M5SonarScopeMinimumRangeMeters, M5SonarScopeMaximumRangeMeters);
    return result;
}
} // namespace DeepRun::Game::Combat
