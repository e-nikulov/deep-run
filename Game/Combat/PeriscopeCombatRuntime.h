#pragma once

#include "Game/Combat/PeriscopeObservationSystem.h"
#include "Game/Combat/PlayerCombatCommandRuntime.h"
#include "Simulation/Perception/TrackManager.h"

#include <cmath>
#include <expected>
#include <optional>
#include <string>

namespace DeepRun::Game::Combat
{
[[nodiscard]] inline std::optional<Perception::Track> FindPeriscopeTrack(
    const Perception::TrackManager& tracks,
    const std::optional<std::uint64_t> trackId)
{
    if (!trackId)
    {
        return std::nullopt;
    }
    for (const auto& track : tracks.Tracks())
    {
        if (track.trackId == *trackId && track.lifecycle != Perception::TrackLifecycleState::Lost)
        {
            return track;
        }
    }
    return std::nullopt;
}

[[nodiscard]] inline PlayerCombatCommandFeedback TogglePeriscopeForSelectedTrack(
    PeriscopeState& periscope,
    const Perception::TrackManager& tracks,
    const std::optional<std::uint64_t> selectedTrackId,
    const float ownshipDepthMeters)
{
    if (periscope.raised)
    {
        periscope.raised = false;
        return PlayerCombatCommandFeedback{
            .command = PlayerCombatCommandType::TogglePeriscope,
            .accepted = true,
            .trackId = selectedTrackId,
            .message = "periscope stowed"};
    }
    if (!std::isfinite(ownshipDepthMeters) || ownshipDepthMeters < 0.0F ||
        ownshipDepthMeters > PeriscopeObservationConfig{}.maximumOperatingDepthMeters)
    {
        return PlayerCombatCommandFeedback{
            .command = PlayerCombatCommandType::TogglePeriscope,
            .accepted = false,
            .trackId = selectedTrackId,
            .message = "periscope unavailable: ownship is below the 20 m operating zone"};
    }

    if (const auto selected = FindPeriscopeTrack(tracks, selectedTrackId))
    {
        periscope.viewBearingRadians = selected->estimatedBearingRadians;
    }
    periscope.raised = true;
    return PlayerCombatCommandFeedback{
        .command = PlayerCombatCommandType::TogglePeriscope,
        .accepted = true,
        .trackId = selectedTrackId,
        .message = "periscope raised; mast exposed"};
}

[[nodiscard]] inline std::expected<PlayerCombatCommandFeedback, std::string> VisualIdentifySelectedTrackWithConfig(
    const PeriscopeObservationConfig& config,
    PeriscopeState& periscope,
    Perception::TrackManager& tracks,
    const std::optional<std::uint64_t> selectedTrackId,
    const Physics::PhysicsVector3& ownshipPositionMeters,
    const float ownshipDepthMeters,
    const float surfaceLevelYMeters,
    const PeriscopeTargetTruth& targetTruth,
    const double simulationTimeSeconds,
    const PeriscopeOpticalConditions& opticalConditions = {})
{
    const auto selected = FindPeriscopeTrack(tracks, selectedTrackId);
    if (!selected)
    {
        return PlayerCombatCommandFeedback{
            .command = PlayerCombatCommandType::VisualIdentify,
            .accepted = false,
            .trackId = selectedTrackId,
            .message = "visual identification requires a selected perceived contact"};
    }
    if (!periscope.raised)
    {
        return PlayerCombatCommandFeedback{
            .command = PlayerCombatCommandType::VisualIdentify,
            .accepted = false,
            .trackId = selectedTrackId,
            .message = "visual identification requires the periscope to be raised"};
    }

    // The optics are deliberately cued from perceived bearing, never target truth. Ground truth enters only the
    // optical sensor simulator below, exactly like reflector/emitter truth enters acoustic simulation.
    periscope.viewBearingRadians = selected->estimatedBearingRadians;
    const auto observation = ObserveThroughPeriscope(
        config,
        periscope,
        ownshipPositionMeters,
        ownshipDepthMeters,
        surfaceLevelYMeters,
        targetTruth,
        simulationTimeSeconds,
        opticalConditions);
    if (!observation)
    {
        return std::unexpected("periscope optical simulation failed: " + observation.error());
    }
    if (!observation->has_value())
    {
        return PlayerCombatCommandFeedback{
            .command = PlayerCombatCommandType::VisualIdentify,
            .accepted = false,
            .trackId = selectedTrackId,
            .message = ownshipDepthMeters > config.maximumOperatingDepthMeters
                ? "visual identification unavailable: ownship is too deep"
                : "selected contact is outside current optical visibility or field of view"};
    }

    const auto fusedTrackId = tracks.IntegrateObservation(**observation);
    if (!fusedTrackId)
    {
        return std::unexpected("periscope optical evidence failed TrackManager integration: " + fusedTrackId.error());
    }
    if (*fusedTrackId != selected->trackId)
    {
        return std::unexpected("periscope optical evidence associated with a different perceived Track");
    }

    const auto detail = (*observation)->opticalIdentificationLevel;
    if (detail == Perception::OpticalIdentificationLevel::Detected)
    {
        return PlayerCombatCommandFeedback{
            .command = PlayerCombatCommandType::VisualIdentify,
            .accepted = true,
            .trackId = selected->trackId,
            .message = "visual contact detected; type unresolved at current range/conditions"};
    }

    const auto classification = (*observation)->classificationEvidence.value_or(
        Perception::ContactClassification::Unknown);
    const char* label = classification == Perception::ContactClassification::CivilianSurfaceVessel
        ? "CIVILIAN; normal fire inhibited"
        : classification == Perception::ContactClassification::MilitarySurfaceCombatant
            ? "MILITARY surface combatant"
            : "UNCONFIRMED";
    const char* detailLabel = detail == Perception::OpticalIdentificationLevel::FlagOrMarkingsResolved
        ? "flag/markings resolved"
        : "type resolved";
    return PlayerCombatCommandFeedback{
        .command = PlayerCombatCommandType::VisualIdentify,
        .accepted = true,
        .trackId = selected->trackId,
        .message = std::string("visual identification: ") + label + " (" + detailLabel + ")"};
}

[[nodiscard]] inline std::expected<PlayerCombatCommandFeedback, std::string> VisualIdentifySelectedTrack(
    PeriscopeState& periscope,
    Perception::TrackManager& tracks,
    const std::optional<std::uint64_t> selectedTrackId,
    const Physics::PhysicsVector3& ownshipPositionMeters,
    const float ownshipDepthMeters,
    const float surfaceLevelYMeters,
    const PeriscopeTargetTruth& targetTruth,
    const double simulationTimeSeconds,
    const PeriscopeOpticalConditions& opticalConditions = {})
{
    return VisualIdentifySelectedTrackWithConfig(
        PeriscopeObservationConfig{}, periscope, tracks, selectedTrackId, ownshipPositionMeters,
        ownshipDepthMeters, surfaceLevelYMeters, targetTruth, simulationTimeSeconds, opticalConditions);
}

inline void ApplyPeriscopePresentation(
    PlayerCombatPresentationSnapshot& presentation,
    const PeriscopeState& periscope,
    const float ownshipDepthMeters) noexcept
{
    const auto state = BuildPeriscopePresentationSnapshot(
        PeriscopeObservationConfig{}, periscope, ownshipDepthMeters);
    presentation.periscopeWithinOperatingDepth = state.withinOperatingDepth;
    presentation.periscopeRaised = state.raised;
    presentation.periscopeMastExposed = state.mastExposed;
    presentation.periscopeViewBearingRadians = state.raised
        ? std::optional<float>{state.viewBearingRadians}
        : std::nullopt;
    presentation.canVisualIdentify = state.withinOperatingDepth && state.raised &&
        presentation.selectedTrackPresent &&
        presentation.selectedTrackLifecycle != std::optional<Perception::TrackLifecycleState>{Perception::TrackLifecycleState::Lost};
}
} // namespace DeepRun::Game::Combat
