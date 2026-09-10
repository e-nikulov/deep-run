#pragma once

#include "Simulation/Weapons/WeaponRuntime.h"

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace DeepRun::Game::Combat
{
// M5-J1 is deliberately a Game-owned commander interaction boundary, not a generic Engine command bus.
// Commands operate only on perceived Tracks and the existing authoritative WeaponRuntimeState.
enum class PlayerCombatCommandType
{
    SelectNextTrack,
    PrepareWeapon,
    FireWeapon,
};

struct PlayerCombatCommand final
{
    PlayerCombatCommandType type = PlayerCombatCommandType::SelectNextTrack;
};

struct PlayerCombatCommandFeedback final
{
    PlayerCombatCommandType command = PlayerCombatCommandType::SelectNextTrack;
    bool accepted = false;
    std::optional<std::uint64_t> trackId{};
    std::string message{};
};

// Read-only state intended for a later controller-first combat UI. It deliberately exposes perceived quality
// rather than hostile Transform/entity truth and cannot mutate weapon, TrackManager, physics, or simulation.
struct PlayerCombatPresentationSnapshot final
{
    Weapons::WeaponPhase weaponPhase = Weapons::WeaponPhase::Stored;
    std::optional<std::uint64_t> weaponTargetTrackId{};
    std::optional<std::uint64_t> selectedTrackId{};
    bool selectedTrackPresent = false;
    std::optional<Perception::TrackLifecycleState> selectedTrackLifecycle{};
    std::optional<float> selectedTrackConfidence{};
    std::optional<float> selectedBearingUncertaintyRadians{};
    std::optional<float> selectedPositionUncertaintyMeters{};
    bool selectedTrackHasEstimatedPosition = false;
    bool selectedTrackWeaponQualified = false;
    bool canPrepareWeapon = false;
    bool canFireWeapon = false;

    // M5-J3 is populated by CombatPlaygroundRuntime from a dedicated passive-acoustic perceived-world path.
    // No hostile transform, range estimate, weapon runtime pointer, or PhysicsBodyHandle is exposed to UI.
    bool incomingThreatDetected = false;
    std::optional<Perception::TrackLifecycleState> incomingThreatLifecycle{};
    std::optional<float> incomingThreatBearingRadians{};
    std::optional<float> incomingThreatBearingUncertaintyRadians{};
    std::optional<float> incomingThreatConfidence{};
    std::optional<PlayerCombatCommandFeedback> lastCommand{};
};

class PlayerCombatCommandRuntime final
{
public:
    [[nodiscard]] static std::expected<PlayerCombatCommandRuntime, std::string> Create(
        Weapons::WeaponDefinition definition,
        const double simulationTimeSeconds)
    {
        auto weapon = Weapons::CreateWeaponRuntime(definition, simulationTimeSeconds);
        if (!weapon)
        {
            return std::unexpected("M5-J1 player combat weapon creation failed: " + weapon.error());
        }
        return PlayerCombatCommandRuntime(std::move(definition), std::move(*weapon));
    }

    [[nodiscard]] std::expected<void, std::string> Advance(const double simulationTimeSeconds)
    {
        const auto advanced = Weapons::AdvanceWeaponReadiness(definition_, weapon_, simulationTimeSeconds);
        if (!advanced)
        {
            return std::unexpected("M5-J1 player combat readiness advance failed: " + advanced.error());
        }
        return {};
    }

    [[nodiscard]] std::expected<PlayerCombatCommandFeedback, std::string> Execute(
        const PlayerCombatCommand command,
        const std::span<const Perception::Track> tracks,
        const double simulationTimeSeconds)
    {
        // Readiness is SimulationTime-authoritative even when a command is rejected for gameplay reasons.
        const auto advanced = Advance(simulationTimeSeconds);
        if (!advanced)
        {
            return std::unexpected(advanced.error());
        }

        switch (command.type)
        {
        case PlayerCombatCommandType::SelectNextTrack:
            return SelectNextTrack(tracks);
        case PlayerCombatCommandType::PrepareWeapon:
            return Prepare(simulationTimeSeconds);
        case PlayerCombatCommandType::FireWeapon:
            return Fire(tracks, simulationTimeSeconds);
        }
        return std::unexpected("M5-J1 received an unknown player combat command");
    }

    [[nodiscard]] PlayerCombatPresentationSnapshot BuildPresentationSnapshot(
        const std::span<const Perception::Track> tracks) const
    {
        PlayerCombatPresentationSnapshot snapshot{
            .weaponPhase = weapon_.phase,
            .weaponTargetTrackId = weapon_.targetTrackId,
            .selectedTrackId = selectedTrackId_,
            .canPrepareWeapon = weapon_.phase == Weapons::WeaponPhase::Stored,
            .lastCommand = lastCommand_};

        const Perception::Track* selected = FindTrack(tracks, selectedTrackId_);
        if (selected == nullptr)
        {
            return snapshot;
        }

        snapshot.selectedTrackPresent = true;
        snapshot.selectedTrackLifecycle = selected->lifecycle;
        snapshot.selectedTrackConfidence = selected->confidence;
        snapshot.selectedBearingUncertaintyRadians = selected->bearingUncertaintyRadians;
        snapshot.selectedPositionUncertaintyMeters = selected->positionUncertaintyMeters;
        snapshot.selectedTrackHasEstimatedPosition = selected->estimatedPositionMeters.has_value();
        snapshot.selectedTrackWeaponQualified = Weapons::ValidateTrackForWeapon(definition_, *selected).has_value();
        snapshot.canFireWeapon = weapon_.phase == Weapons::WeaponPhase::Ready &&
                                 snapshot.selectedTrackWeaponQualified;
        return snapshot;
    }

    [[nodiscard]] const Weapons::WeaponDefinition& WeaponDefinition() const noexcept { return definition_; }
    [[nodiscard]] const Weapons::WeaponRuntimeState& Weapon() const noexcept { return weapon_; }
    [[nodiscard]] const std::optional<std::uint64_t>& SelectedTrackId() const noexcept { return selectedTrackId_; }
    [[nodiscard]] const std::optional<PlayerCombatCommandFeedback>& LastCommand() const noexcept { return lastCommand_; }

private:
    PlayerCombatCommandRuntime(
        Weapons::WeaponDefinition definition,
        Weapons::WeaponRuntimeState weapon)
        : definition_(std::move(definition)), weapon_(std::move(weapon))
    {
    }

    [[nodiscard]] static bool IsSelectableTrack(const Perception::Track& track) noexcept
    {
        // Commander inspection is intentionally broader than weapon qualification: a bearing-only, tentative or
        // coasting contact may be worth inspecting even though FireWeapon must reject it later.
        return track.trackId != 0U && track.lifecycle != Perception::TrackLifecycleState::Lost;
    }

    [[nodiscard]] static const Perception::Track* FindTrack(
        const std::span<const Perception::Track> tracks,
        const std::optional<std::uint64_t> trackId) noexcept
    {
        if (!trackId)
        {
            return nullptr;
        }
        for (const auto& track : tracks)
        {
            if (track.trackId == *trackId)
            {
                return &track;
            }
        }
        return nullptr;
    }

    [[nodiscard]] std::expected<PlayerCombatCommandFeedback, std::string> SelectNextTrack(
        const std::span<const Perception::Track> tracks)
    {
        std::optional<std::uint64_t> lowest{};
        std::optional<std::uint64_t> next{};
        for (const auto& track : tracks)
        {
            if (!IsSelectableTrack(track))
            {
                continue;
            }
            if (!lowest || track.trackId < *lowest)
            {
                lowest = track.trackId;
            }
            if (selectedTrackId_ && track.trackId > *selectedTrackId_ && (!next || track.trackId < *next))
            {
                next = track.trackId;
            }
        }

        if (!lowest)
        {
            return RecordRejected(PlayerCombatCommandType::SelectNextTrack, std::nullopt,
                                  "no perceived track is currently selectable");
        }

        selectedTrackId_ = selectedTrackId_ ? next.value_or(*lowest) : *lowest;
        return RecordAccepted(PlayerCombatCommandType::SelectNextTrack, selectedTrackId_,
                              "selected perceived track");
    }

    [[nodiscard]] std::expected<PlayerCombatCommandFeedback, std::string> Prepare(
        const double simulationTimeSeconds)
    {
        if (weapon_.phase != Weapons::WeaponPhase::Stored)
        {
            return RecordRejected(PlayerCombatCommandType::PrepareWeapon, selectedTrackId_,
                                  "weapon can only begin preparation from Stored state");
        }

        const auto prepared = Weapons::PrepareWeapon(definition_, weapon_, simulationTimeSeconds);
        if (!prepared)
        {
            return std::unexpected("M5-J1 weapon preparation failed: " + prepared.error());
        }
        return RecordAccepted(PlayerCombatCommandType::PrepareWeapon, selectedTrackId_,
                              "weapon preparation ordered");
    }

    [[nodiscard]] std::expected<PlayerCombatCommandFeedback, std::string> Fire(
        const std::span<const Perception::Track> tracks,
        const double simulationTimeSeconds)
    {
        if (weapon_.phase != Weapons::WeaponPhase::Ready)
        {
            return RecordRejected(PlayerCombatCommandType::FireWeapon, selectedTrackId_,
                                  "weapon is not Ready");
        }
        const Perception::Track* selected = FindTrack(tracks, selectedTrackId_);
        if (selected == nullptr)
        {
            return RecordRejected(PlayerCombatCommandType::FireWeapon, selectedTrackId_,
                                  "selected perceived track is no longer present");
        }

        // Revalidate the exact current Track on the fire tick. A stale target ID or formerly-good estimate can
        // never authorize launch. Use a candidate copy so rejected assignment/launch cannot partially mutate state.
        Weapons::WeaponRuntimeState candidate = weapon_;
        const auto assigned = Weapons::AssignWeaponTarget(
            definition_, candidate, *selected, simulationTimeSeconds);
        if (!assigned)
        {
            return RecordRejected(PlayerCombatCommandType::FireWeapon, selectedTrackId_, assigned.error());
        }
        const auto launched = Weapons::LaunchWeapon(definition_, candidate, simulationTimeSeconds);
        if (!launched)
        {
            return std::unexpected("M5-J1 launch failed after successful current-track validation: " + launched.error());
        }

        weapon_ = std::move(candidate);
        return RecordAccepted(PlayerCombatCommandType::FireWeapon, selectedTrackId_, "weapon launch ordered");
    }

    [[nodiscard]] PlayerCombatCommandFeedback RecordAccepted(
        const PlayerCombatCommandType command,
        const std::optional<std::uint64_t> trackId,
        std::string message)
    {
        lastCommand_ = PlayerCombatCommandFeedback{
            .command = command,
            .accepted = true,
            .trackId = trackId,
            .message = std::move(message)};
        return *lastCommand_;
    }

    [[nodiscard]] PlayerCombatCommandFeedback RecordRejected(
        const PlayerCombatCommandType command,
        const std::optional<std::uint64_t> trackId,
        std::string message)
    {
        lastCommand_ = PlayerCombatCommandFeedback{
            .command = command,
            .accepted = false,
            .trackId = trackId,
            .message = std::move(message)};
        return *lastCommand_;
    }

    Weapons::WeaponDefinition definition_{};
    Weapons::WeaponRuntimeState weapon_{};
    std::optional<std::uint64_t> selectedTrackId_{};
    std::optional<PlayerCombatCommandFeedback> lastCommand_{};
};
} // namespace DeepRun::Game::Combat
