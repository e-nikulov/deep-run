#pragma once

#include "Engine/Core/TimeCompression.h"
#include "Game/Combat/PlayerCombatCommandRuntime.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>

namespace DeepRun::Game::Combat
{
// Player-time pacing telemetry. These values intentionally measure perceived waiting time rather than raw
// SimulationTime: a 40 s operational transit at 8x contributes roughly 5 s to the pacing clock. The telemetry is
// read-only and must never authorize gameplay, targeting, classification or weapon state.
struct GameplayPacingMetricsSnapshot final
{
    double playerElapsedSeconds = 0.0;
    std::optional<double> timeToFirstContactSeconds{};
    std::optional<double> timeToClassificationSeconds{};
    std::optional<double> timeToFirstWeaponLaunchSeconds{};
    double longestNoDecisionIntervalSeconds = 0.0;
    double currentNoDecisionIntervalSeconds = 0.0;
    std::uint64_t playerDecisionCount = 0U;
};

class GameplayPacingMetrics final
{
public:
    [[nodiscard]] std::expected<void, std::string> Observe(
        const std::span<const Perception::Track> playerTracks,
        const PlayerCombatPresentationSnapshot& playerCombat,
        const std::span<const PlayerCombatCommand> commands,
        const bool hadSelectedTrackBeforeCommands,
        const double simulationTimeSeconds,
        const Core::TimeCompressionRate effectiveRate)
    {
        if (!std::isfinite(simulationTimeSeconds) || simulationTimeSeconds < 0.0 ||
            !Core::IsValidTimeCompressionRate(effectiveRate))
        {
            return std::unexpected("gameplay pacing telemetry input is invalid");
        }

        if (!initialized_)
        {
            initialized_ = true;
            lastSimulationTimeSeconds_ = simulationTimeSeconds;
            previousEffectiveRate_ = effectiveRate;
        }
        else
        {
            if (simulationTimeSeconds < lastSimulationTimeSeconds_)
            {
                return std::unexpected("gameplay pacing telemetry SimulationTime must be monotonic");
            }
            const double simulationDeltaSeconds = simulationTimeSeconds - lastSimulationTimeSeconds_;
            playerElapsedSeconds_ += simulationDeltaSeconds /
                Core::TimeCompressionMultiplier(previousEffectiveRate_);
            lastSimulationTimeSeconds_ = simulationTimeSeconds;
            previousEffectiveRate_ = effectiveRate;
        }

        if (!timeToFirstContactSeconds_ && HasPerceivedContact(playerTracks))
        {
            timeToFirstContactSeconds_ = playerElapsedSeconds_;
        }
        if (!timeToClassificationSeconds_ && HasClassifiedContact(playerTracks))
        {
            timeToClassificationSeconds_ = playerElapsedSeconds_;
        }
        if (!timeToFirstWeaponLaunchSeconds_ && playerCombat.weaponPhase == Weapons::WeaponPhase::Launched)
        {
            timeToFirstWeaponLaunchSeconds_ = playerElapsedSeconds_;
        }

        const std::size_t decisionCount = CountPlayerDecisions(commands, hadSelectedTrackBeforeCommands);
        if (decisionCount > 0U)
        {
            longestCompletedNoDecisionIntervalSeconds_ = std::max(
                longestCompletedNoDecisionIntervalSeconds_,
                playerElapsedSeconds_ - lastDecisionPlayerTimeSeconds_);
            lastDecisionPlayerTimeSeconds_ = playerElapsedSeconds_;
            playerDecisionCount_ += static_cast<std::uint64_t>(decisionCount);
        }

        return {};
    }

    [[nodiscard]] GameplayPacingMetricsSnapshot Snapshot() const noexcept
    {
        const double currentNoDecisionIntervalSeconds = initialized_
            ? std::max(0.0, playerElapsedSeconds_ - lastDecisionPlayerTimeSeconds_)
            : 0.0;
        return GameplayPacingMetricsSnapshot{
            .playerElapsedSeconds = playerElapsedSeconds_,
            .timeToFirstContactSeconds = timeToFirstContactSeconds_,
            .timeToClassificationSeconds = timeToClassificationSeconds_,
            .timeToFirstWeaponLaunchSeconds = timeToFirstWeaponLaunchSeconds_,
            .longestNoDecisionIntervalSeconds = std::max(
                longestCompletedNoDecisionIntervalSeconds_, currentNoDecisionIntervalSeconds),
            .currentNoDecisionIntervalSeconds = currentNoDecisionIntervalSeconds,
            .playerDecisionCount = playerDecisionCount_};
    }

private:
    [[nodiscard]] static bool HasPerceivedContact(const std::span<const Perception::Track> tracks) noexcept
    {
        return std::any_of(tracks.begin(), tracks.end(), [](const Perception::Track& track) {
            return track.trackId != 0U && track.lifecycle != Perception::TrackLifecycleState::Lost;
        });
    }

    [[nodiscard]] static bool HasClassifiedContact(const std::span<const Perception::Track> tracks) noexcept
    {
        return std::any_of(tracks.begin(), tracks.end(), [](const Perception::Track& track) {
            return track.trackId != 0U && track.lifecycle != Perception::TrackLifecycleState::Lost &&
                   track.classification != Perception::ContactClassification::Unknown;
        });
    }

    [[nodiscard]] static std::size_t CountPlayerDecisions(
        const std::span<const PlayerCombatCommand> commands,
        const bool hadSelectedTrackBeforeCommands) noexcept
    {
        if (commands.empty())
        {
            return 0U;
        }

        std::size_t count = commands.size();
        // Main's controller-first composition appends one SelectNextTrack fallback only while there is no
        // current selection. Do not let that automatic convenience command reset the player's idle interval.
        // A manually requested SelectNextTrack remains counted: when no selection existed it precedes the
        // fallback, producing two commands; once a selection exists no fallback is appended at all.
        if (!hadSelectedTrackBeforeCommands && commands.back().type == PlayerCombatCommandType::SelectNextTrack)
        {
            --count;
        }
        return count;
    }

    bool initialized_ = false;
    double lastSimulationTimeSeconds_ = 0.0;
    Core::TimeCompressionRate previousEffectiveRate_ = Core::TimeCompressionRate::X1;
    double playerElapsedSeconds_ = 0.0;
    double lastDecisionPlayerTimeSeconds_ = 0.0;
    double longestCompletedNoDecisionIntervalSeconds_ = 0.0;
    std::optional<double> timeToFirstContactSeconds_{};
    std::optional<double> timeToClassificationSeconds_{};
    std::optional<double> timeToFirstWeaponLaunchSeconds_{};
    std::uint64_t playerDecisionCount_ = 0U;
};
} // namespace DeepRun::Game::Combat
