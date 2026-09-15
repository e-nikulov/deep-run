#include "Engine/Core/FixedStepAccumulator.h"
#include "Engine/Core/TimeCompression.h"
#include "Game/Combat/CombatTimeCompressionPolicy.h"
#include "Game/Combat/GameplayPacingMetrics.h"

#include <array>
#include <cmath>
#include <iostream>
#include <span>

namespace
{
using DeepRun::Core::CurrentTimeCompressionEffectiveRate;
using DeepRun::Core::FixedStepAccumulator;
using DeepRun::Core::PublishTimeCompressionSafetyCap;
using DeepRun::Core::ShouldInterruptCompressedFixedPacket;
using DeepRun::Core::TimeCompressionController;
using DeepRun::Core::TimeCompressionMultiplier;
using DeepRun::Core::TimeCompressionRate;
using DeepRun::Core::TimeCompressionSafetyScope;
using DeepRun::Game::Combat::CombatTimeCompressionSignals;
using DeepRun::Game::Combat::GameplayPacingMetrics;
using DeepRun::Game::Combat::PlayerCombatCommand;
using DeepRun::Game::Combat::PlayerCombatCommandType;
using DeepRun::Game::Combat::PlayerCombatPresentationSnapshot;
using DeepRun::Game::Combat::ResolveCombatTimeCompressionMaximum;
using DeepRun::Perception::ContactClassification;
using DeepRun::Perception::Track;
using DeepRun::Perception::TrackLifecycleState;
using DeepRun::Weapons::P700GranitPhase;
using DeepRun::Weapons::WeaponPhase;

[[nodiscard]] bool Expect(const bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "Time compression test failed: " << message << '\n';
        return false;
    }
    return true;
}

[[nodiscard]] bool ExpectNear(
    const double actual,
    const double expected,
    const double tolerance,
    const char* message)
{
    return Expect(std::abs(actual - expected) <= tolerance, message);
}

[[nodiscard]] std::uint32_t StepsForOneRealFrame(
    const TimeCompressionController& controller,
    const double realDeltaSeconds = 1.0 / 60.0)
{
    FixedStepAccumulator accumulator(1.0 / 60.0);
    return accumulator.Accumulate(realDeltaSeconds * controller.EffectiveMultiplier());
}

[[nodiscard]] bool RunCombatPolicyChecks()
{
    if (!Expect(
            ResolveCombatTimeCompressionMaximum({}) == TimeCompressionRate::X8,
            "quiet combat state must permit 8x") ||
        !Expect(
            ResolveCombatTimeCompressionMaximum(
                CombatTimeCompressionSignals{.hasPerceivedContact = true}) == TimeCompressionRate::X4,
            "perceived contact must cap at 4x") ||
        !Expect(
            ResolveCombatTimeCompressionMaximum(
                CombatTimeCompressionSignals{.selectedTrackWeaponQualified = true}) == TimeCompressionRate::X2,
            "qualified firing solution must cap at 2x") ||
        !Expect(
            ResolveCombatTimeCompressionMaximum(
                CombatTimeCompressionSignals{.conventionalTorpedoInFlight = true}) == TimeCompressionRate::X2,
            "conventional torpedo in flight must cap at 2x") ||
        !Expect(
            ResolveCombatTimeCompressionMaximum(
                CombatTimeCompressionSignals{.incomingThreatDetected = true}) == TimeCompressionRate::X1,
            "incoming threat must force 1x") ||
        !Expect(
            ResolveCombatTimeCompressionMaximum(
                CombatTimeCompressionSignals{
                    .incomingThreatDetected = true,
                    .hasPerceivedContact = true,
                    .selectedTrackWeaponQualified = true,
                    .p700Phase = P700GranitPhase::Cruise}) == TimeCompressionRate::X1,
            "strict incoming-threat cap must dominate softer combat signals") ||
        !Expect(
            ResolveCombatTimeCompressionMaximum(
                CombatTimeCompressionSignals{.importantImpactEvent = true}) == TimeCompressionRate::X1,
            "impact event must force 1x") ||
        !Expect(
            ResolveCombatTimeCompressionMaximum(
                CombatTimeCompressionSignals{.playerDestroyed = true}) == TimeCompressionRate::X1,
            "destroyed player state must force 1x") ||
        !Expect(
            ResolveCombatTimeCompressionMaximum(
                CombatTimeCompressionSignals{.p700Phase = P700GranitPhase::HatchOpening}) ==
                TimeCompressionRate::X1,
            "P-700 launch sequence must force 1x") ||
        !Expect(
            ResolveCombatTimeCompressionMaximum(
                CombatTimeCompressionSignals{.p700Phase = P700GranitPhase::Cruise}) ==
                TimeCompressionRate::X4,
            "P-700 cruise must permit 4x") ||
        !Expect(
            ResolveCombatTimeCompressionMaximum(
                CombatTimeCompressionSignals{.p700Phase = P700GranitPhase::Terminal}) ==
                TimeCompressionRate::X1,
            "P-700 terminal must force 1x"))
    {
        return false;
    }
    return true;
}

[[nodiscard]] bool RunGameplayPacingChecks()
{
    GameplayPacingMetrics metrics;
    PlayerCombatPresentationSnapshot combat{};
    const std::span<const Track> noTracks{};
    const std::span<const PlayerCombatCommand> noCommands{};

    if (const auto observed = metrics.Observe(
            noTracks, combat, noCommands, false, 0.0, TimeCompressionRate::X8);
        !observed)
    {
        return Expect(false, "pacing telemetry must accept its first 8x sample");
    }

    Track contact{
        .trackId = 1U,
        .lifecycle = TrackLifecycleState::Confirmed,
        .classification = ContactClassification::Unknown};
    std::array<Track, 1> tracks{contact};
    if (const auto observed = metrics.Observe(
            std::span<const Track>{tracks}, combat, noCommands, false, 8.0, TimeCompressionRate::X8);
        !observed)
    {
        return Expect(false, "pacing telemetry must accept first-contact sample");
    }
    auto snapshot = metrics.Snapshot();
    if (!ExpectNear(snapshot.playerElapsedSeconds, 1.0, 1.0e-9,
                    "8 s SimulationTime at 8x must equal 1 s player time") ||
        !Expect(snapshot.timeToFirstContactSeconds.has_value(), "first contact metric must be populated") ||
        !ExpectNear(*snapshot.timeToFirstContactSeconds, 1.0, 1.0e-9,
                    "TimeToFirstContact must use player time") ||
        !Expect(!snapshot.timeToClassificationSeconds.has_value(),
                "unknown contact must not satisfy classification metric") ||
        !ExpectNear(snapshot.longestNoDecisionIntervalSeconds, 1.0, 1.0e-9,
                    "idle interval must grow before the first decision"))
    {
        return false;
    }

    tracks[0].classification = ContactClassification::MilitarySurfaceCombatant;
    const std::array<PlayerCombatCommand, 1> activeDecision{
        PlayerCombatCommand{.type = PlayerCombatCommandType::ActiveSonarPing}};
    if (const auto observed = metrics.Observe(
            std::span<const Track>{tracks}, combat, std::span<const PlayerCombatCommand>{activeDecision},
            false, 16.0, TimeCompressionRate::X8);
        !observed)
    {
        return Expect(false, "pacing telemetry must accept classification/decision sample");
    }
    snapshot = metrics.Snapshot();
    if (!Expect(snapshot.timeToClassificationSeconds.has_value(), "classification metric must be populated") ||
        !ExpectNear(*snapshot.timeToClassificationSeconds, 2.0, 1.0e-9,
                    "TimeToClassification must use compressed player time") ||
        !Expect(snapshot.playerDecisionCount == 1U, "semantic player command must count as a decision") ||
        !ExpectNear(snapshot.longestNoDecisionIntervalSeconds, 2.0, 1.0e-9,
                    "first decision must close the initial idle interval") ||
        !ExpectNear(snapshot.currentNoDecisionIntervalSeconds, 0.0, 1.0e-9,
                    "decision must reset the current idle interval"))
    {
        return false;
    }

    combat.weaponPhase = WeaponPhase::Launched;
    if (const auto observed = metrics.Observe(
            std::span<const Track>{tracks}, combat, noCommands, true, 20.0, TimeCompressionRate::X2);
        !observed)
    {
        return Expect(false, "pacing telemetry must accept first-launch sample");
    }
    snapshot = metrics.Snapshot();
    if (!Expect(snapshot.timeToFirstWeaponLaunchSeconds.has_value(), "first launch metric must be populated") ||
        !ExpectNear(*snapshot.timeToFirstWeaponLaunchSeconds, 2.5, 1.0e-9,
                    "rate switch applies after the interval paced by the previous 8x tick"))
    {
        return false;
    }

    combat.weaponPhase = WeaponPhase::Stored;
    const std::array<PlayerCombatCommand, 1> automaticSelection{
        PlayerCombatCommand{.type = PlayerCombatCommandType::SelectNextTrack}};
    if (const auto observed = metrics.Observe(
            std::span<const Track>{tracks}, combat, std::span<const PlayerCombatCommand>{automaticSelection},
            false, 24.0, TimeCompressionRate::X2);
        !observed)
    {
        return Expect(false, "pacing telemetry must accept automatic-selection sample");
    }
    snapshot = metrics.Snapshot();
    if (!Expect(snapshot.playerDecisionCount == 1U,
                "automatic initial contact selection must not count as a player decision") ||
        !ExpectNear(snapshot.currentNoDecisionIntervalSeconds, 2.5, 1.0e-9,
                    "automatic target selection must not reset player idle time") ||
        !ExpectNear(snapshot.longestNoDecisionIntervalSeconds, 2.5, 1.0e-9,
                    "live idle interval must participate in LongestNoDecisionInterval"))
    {
        return false;
    }

    const std::array<PlayerCombatCommand, 1> manualSelection{
        PlayerCombatCommand{.type = PlayerCombatCommandType::SelectNextTrack}};
    if (const auto observed = metrics.Observe(
            std::span<const Track>{tracks}, combat, std::span<const PlayerCombatCommand>{manualSelection},
            true, 26.0, TimeCompressionRate::X2);
        !observed)
    {
        return Expect(false, "pacing telemetry must accept manual selection sample");
    }
    snapshot = metrics.Snapshot();
    if (!Expect(snapshot.playerDecisionCount == 2U, "manual contact cycling must count as a decision") ||
        !ExpectNear(snapshot.longestNoDecisionIntervalSeconds, 3.5, 1.0e-9,
                    "manual selection must close the accumulated idle interval") ||
        !ExpectNear(snapshot.currentNoDecisionIntervalSeconds, 0.0, 1.0e-9,
                    "manual selection must reset current idle time") ||
        !ExpectNear(*snapshot.timeToFirstContactSeconds, 1.0, 1.0e-9,
                    "first-contact metric must remain sticky") ||
        !ExpectNear(*snapshot.timeToClassificationSeconds, 2.0, 1.0e-9,
                    "classification metric must remain sticky") ||
        !ExpectNear(*snapshot.timeToFirstWeaponLaunchSeconds, 2.5, 1.0e-9,
                    "first-launch metric must remain sticky"))
    {
        return false;
    }

    if (const auto observed = metrics.Observe(
            std::span<const Track>{tracks}, combat, noCommands, true, 25.0, TimeCompressionRate::X2);
        observed)
    {
        return Expect(false, "pacing telemetry must reject time reversal");
    }
    if (const auto observed = metrics.Observe(
            std::span<const Track>{tracks}, combat, noCommands, true, 26.0,
            static_cast<TimeCompressionRate>(255U));
        observed)
    {
        return Expect(false, "pacing telemetry must reject invalid compression rates");
    }

    return true;
}
}

int main()
{
    TimeCompressionController controller;

    if (!Expect(controller.RequestedRate() == TimeCompressionRate::X1, "default requested rate must be 1x") ||
        !Expect(controller.EffectiveRate() == TimeCompressionRate::X1, "default effective rate must be 1x") ||
        !Expect(controller.MaximumRate() == TimeCompressionRate::X8, "default safety ceiling must be 8x") ||
        !Expect(StepsForOneRealFrame(controller) == 1U, "1x must accumulate one 60 Hz tick per 1/60 real second"))
    {
        return 1;
    }

    controller.IncreaseRequestedRate();
    if (!Expect(controller.EffectiveRate() == TimeCompressionRate::X2, "first increase must select 2x") ||
        !Expect(StepsForOneRealFrame(controller) == 2U, "2x must accumulate two fixed ticks"))
    {
        return 1;
    }

    controller.IncreaseRequestedRate();
    if (!Expect(controller.EffectiveRate() == TimeCompressionRate::X4, "second increase must select 4x") ||
        !Expect(StepsForOneRealFrame(controller) == 4U, "4x must accumulate four fixed ticks"))
    {
        return 1;
    }

    controller.IncreaseRequestedRate();
    controller.IncreaseRequestedRate();
    if (!Expect(controller.RequestedRate() == TimeCompressionRate::X8, "increase must saturate at 8x") ||
        !Expect(StepsForOneRealFrame(controller) == 8U, "8x must accumulate eight fixed ticks"))
    {
        return 1;
    }

    if (!Expect(controller.SetMaximumRate(TimeCompressionRate::X2), "valid safety ceiling must be accepted") ||
        !Expect(controller.RequestedRate() == TimeCompressionRate::X8, "safety clamp must preserve player intent") ||
        !Expect(controller.EffectiveRate() == TimeCompressionRate::X2, "safety clamp must limit effective rate") ||
        !Expect(StepsForOneRealFrame(controller) == 2U, "safety-clamped rate must drive fixed work") ||
        !Expect(ShouldInterruptCompressedFixedPacket(TimeCompressionRate::X8, TimeCompressionRate::X2),
                "8x packet must interrupt when gameplay tightens to 2x") ||
        !Expect(ShouldInterruptCompressedFixedPacket(TimeCompressionRate::X4, TimeCompressionRate::X1),
                "4x packet must interrupt when gameplay tightens to 1x") ||
        !Expect(!ShouldInterruptCompressedFixedPacket(TimeCompressionRate::X2, TimeCompressionRate::X2),
                "unchanged cap must not interrupt packet") ||
        !Expect(!ShouldInterruptCompressedFixedPacket(TimeCompressionRate::X2, TimeCompressionRate::X4),
                "released cap must not retroactively interrupt packet"))
    {
        return 1;
    }

    if (!Expect(controller.SetMaximumRate(TimeCompressionRate::X8), "8x safety ceiling must be accepted") ||
        !Expect(controller.EffectiveRate() == TimeCompressionRate::X8, "releasing clamp must restore requested rate") ||
        !Expect(!CurrentTimeCompressionEffectiveRate().has_value(),
                "effective-rate telemetry must be unavailable outside fixed-update scope"))
    {
        return 1;
    }

    {
        TimeCompressionSafetyScope scope(controller);
        const auto boundRate = CurrentTimeCompressionEffectiveRate();
        if (!Expect(boundRate.has_value() && *boundRate == TimeCompressionRate::X8,
                    "bound telemetry must expose the current effective rate before gameplay caps") ||
            !Expect(PublishTimeCompressionSafetyCap(TimeCompressionRate::X4), "bound safety publisher must accept 4x") ||
            !Expect(CurrentTimeCompressionEffectiveRate() == TimeCompressionRate::X4,
                    "bound telemetry must reflect a newly tightened cap") ||
            !Expect(PublishTimeCompressionSafetyCap(TimeCompressionRate::X1), "bound safety publisher must accept 1x") ||
            !Expect(PublishTimeCompressionSafetyCap(TimeCompressionRate::X4), "later looser cap must be accepted but not relax") ||
            !Expect(controller.MaximumRate() == TimeCompressionRate::X1,
                    "multiple safety producers must retain the strictest cap") ||
            !Expect(controller.RequestedRate() == TimeCompressionRate::X8,
                    "safety publisher must never rewrite requested player intent"))
        {
            return 1;
        }
    }
    if (!Expect(!PublishTimeCompressionSafetyCap(TimeCompressionRate::X1),
                "publisher outside fixed-update safety scope must be rejected") ||
        !Expect(!CurrentTimeCompressionEffectiveRate().has_value(),
                "effective-rate telemetry must leave scope with the controller binding"))
    {
        return 1;
    }

    if (!Expect(controller.SetMaximumRate(TimeCompressionRate::X8), "test must release safety ceiling") ||
        !RunCombatPolicyChecks() ||
        !RunGameplayPacingChecks())
    {
        return 1;
    }

    controller.BreakToRealtime();
    if (!Expect(controller.RequestedRate() == TimeCompressionRate::X1, "explicit break API must reset player intent to 1x") ||
        !Expect(controller.EffectiveRate() == TimeCompressionRate::X1, "explicit break must become effective immediately"))
    {
        return 1;
    }

    controller.DecreaseRequestedRate();
    if (!Expect(controller.RequestedRate() == TimeCompressionRate::X1, "decrease must saturate at 1x"))
    {
        return 1;
    }

    if (!Expect(!controller.SetRequestedRate(static_cast<TimeCompressionRate>(255U)),
                "invalid requested rate must be rejected") ||
        !Expect(!controller.SetMaximumRate(static_cast<TimeCompressionRate>(255U)),
                "invalid safety ceiling must be rejected") ||
        !Expect(!controller.TightenMaximumRate(static_cast<TimeCompressionRate>(255U)),
                "invalid tightening cap must be rejected") ||
        !Expect(!ShouldInterruptCompressedFixedPacket(
                    static_cast<TimeCompressionRate>(255U), TimeCompressionRate::X1),
                "invalid packet-start rate must not request interruption") ||
        !Expect(TimeCompressionMultiplier(TimeCompressionRate::X8) == 8.0, "8x multiplier contract"))
    {
        return 1;
    }

    std::cout << "Time compression and gameplay pacing telemetry: PASS\n";
    return 0;
}
