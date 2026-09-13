#pragma once

#include "Game/Combat/SonarPresentation.h"
#include "Game/Weapons/PlayerWeaponSelection.h"
#include "Simulation/Weapons/WeaponRuntime.h"

#include <cstddef>
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
    PreviousWeapon,
    NextWeapon,
    PrepareWeapon,
    FireWeapon,
    ActiveSonarPing,
    DeployDecoy,
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

// Read-only state intended for controller-first combat UI. It deliberately exposes perceived quality and
// perceived visual classification rather than hostile Transform/entity truth. A weapon-quality acoustic track
// may remain visually unconfirmed; that is an intentional civilian-identification risk, not a missing field.
struct PlayerCombatPresentationSnapshot final
{
    Armament::PlayerWeaponType selectedWeapon = Armament::PlayerWeaponType::HeavyweightTorpedo;
    std::size_t p700LoadedCount = 0U;
    Weapons::WeaponPhase weaponPhase = Weapons::WeaponPhase::Stored;
    std::optional<std::uint64_t> weaponTargetTrackId{};
    std::optional<std::uint64_t> selectedTrackId{};
    bool selectedTrackPresent = false;
    std::optional<Perception::TrackLifecycleState> selectedTrackLifecycle{};
    std::optional<float> selectedTrackConfidence{};
    std::optional<float> selectedBearingUncertaintyRadians{};
    std::optional<float> selectedPositionUncertaintyMeters{};
    bool selectedTrackHasEstimatedPosition = false;
    std::optional<Physics::PhysicsVector3> selectedTrackEstimatedPositionMeters{};
    Perception::ContactClassification selectedTrackClassification = Perception::ContactClassification::Unknown;
    bool selectedTrackVisuallyIdentified = false;
    bool selectedTrackCivilianRisk = false;
    bool selectedTrackWeaponQualified = false;
    bool selectedTrackRulesOfEngagementQualified = false;
    bool canPrepareWeapon = false;
    bool canFireWeapon = false;
    bool canActiveSonarPing = false;
    bool activeSonarPulsePending = false;
    SonarPresentationSnapshot sonar{};

    // M5-J3 is populated by CombatPlaygroundRuntime from a dedicated passive-acoustic perceived-world path.
    // No hostile transform, range estimate, weapon runtime pointer, or PhysicsBodyHandle is exposed to UI.
    bool incomingThreatDetected = false;
    std::optional<Perception::TrackLifecycleState> incomingThreatLifecycle{};
    std::optional<float> incomingThreatBearingRadians{};
    std::optional<float> incomingThreatBearingUncertaintyRadians{};
    std::optional<float> incomingThreatConfidence{};
    bool canDeployDecoy = false;
    bool playerDecoyActive = false;
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

    // Weapon Selector changes the actual qualification/preparation profile, not merely a UI label. Switching is
    // legal only from Stored so readiness/target state from one weapon type can never bleed into another type.
    [[nodiscard]] std::expected<void, std::string> ReconfigureStoredWeapon(
        Weapons::WeaponDefinition definition,
        const double simulationTimeSeconds)
    {
        if (const auto advanced = Advance(simulationTimeSeconds); !advanced)
        {
            return std::unexpected(advanced.error());
        }
        if (weapon_.phase != Weapons::WeaponPhase::Stored)
        {
            return std::unexpected("M5 weapon selection can only change while the current weapon is Stored");
        }
        auto replacement = Weapons::CreateWeaponRuntime(definition, simulationTimeSeconds);
        if (!replacement)
        {
            return std::unexpected("M5 weapon selection profile creation failed: " + replacement.error());
        }
        definition_ = std::move(definition);
        weapon_ = std::move(*replacement);
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
        case PlayerCombatCommandType::PreviousWeapon:
        case PlayerCombatCommandType::NextWeapon:
            return std::unexpected("M5 Weapon Selector is owned by CombatPlaygroundRuntime, not weapon runtime");
        case PlayerCombatCommandType::PrepareWeapon:
            return Prepare(simulationTimeSeconds);
        case PlayerCombatCommandType::FireWeapon:
            return Fire(tracks, simulationTimeSeconds);
        case PlayerCombatCommandType::ActiveSonarPing:
            return std::unexpected("M5-J5 ActiveSonarPing is owned by CombatPlaygroundRuntime, not weapon runtime");
        case PlayerCombatCommandType::DeployDecoy:
            return std::unexpected("M5-J4 DeployDecoy is owned by CombatPlaygroundRuntime, not weapon runtime");
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
        snapshot.selectedTrackEstimatedPositionMeters = selected->estimatedPositionMeters;
        snapshot.selectedTrackClassification = selected->classification;
        snapshot.selectedTrackVisuallyIdentified = selected->visuallyIdentified;
        snapshot.selectedTrackWeaponQualified = Weapons::ValidateTrackForWeapon(definition_, *selected).has_value();
        snapshot.selectedTrackCivilianRisk = snapshot.selectedTrackWeaponQualified && !selected->visuallyIdentified;
        snapshot.selectedTrackRulesOfEngagementQualified =
            snapshot.selectedTrackWeaponQualified && !IsVisuallyConfirmedCivilian(*selected);
        snapshot.canFireWeapon = weapon_.phase == Weapons::WeaponPhase::Ready &&
                                 snapshot.selectedTrackRulesOfEngagementQualified;
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

    [[nodiscard]] static bool IsVisuallyConfirmedCivilian(const Perception::Track& track) noexcept
    {
        return track.visuallyIdentified &&
               track.classification == Perception::ContactClassification::CivilianSurfaceVessel;
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
        if (IsVisuallyConfirmedCivilian(*selected))
        {
            return RecordRejected(PlayerCombatCommandType::FireWeapon, selectedTrackId_,
                                  "visual identification confirms CIVILIAN vessel; fire inhibited by rules of engagement");
        }

        // Deliberately do NOT require visual identification here. A sufficiently strong acoustic/ranging Track
        // remains employable, but firing it while classification is Unknown carries the gameplay risk that the
        // contact is actually civilian. Visual confirmation removes that uncertainty; it is not omniscient truth.
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
        return RecordAccepted(
            PlayerCombatCommandType::FireWeapon,
            selectedTrackId_,
            selected->visuallyIdentified
                ? "weapon launch ordered against visually identified combatant"
                : "weapon launch ordered WITHOUT visual identification; civilian-risk accepted");
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
