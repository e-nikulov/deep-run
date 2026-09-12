#include "Engine/Core/FixedStepAccumulator.h"
#include "Engine/Core/TimeCompression.h"
#include "Game/Combat/CombatTimeCompressionPolicy.h"

#include <iostream>

namespace
{
using DeepRun::Core::FixedStepAccumulator;
using DeepRun::Core::PublishTimeCompressionSafetyCap;
using DeepRun::Core::TimeCompressionController;
using DeepRun::Core::TimeCompressionMultiplier;
using DeepRun::Core::TimeCompressionRate;
using DeepRun::Core::TimeCompressionSafetyScope;
using DeepRun::Game::Combat::CombatTimeCompressionSignals;
using DeepRun::Game::Combat::ResolveCombatTimeCompressionMaximum;
using DeepRun::Weapons::P700GranitPhase;

[[nodiscard]] bool Expect(const bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "Time compression test failed: " << message << '\n';
        return false;
    }
    return true;
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
        !Expect(StepsForOneRealFrame(controller) == 2U, "safety-clamped rate must drive fixed work"))
    {
        return 1;
    }

    if (!Expect(controller.SetMaximumRate(TimeCompressionRate::X8), "8x safety ceiling must be accepted") ||
        !Expect(controller.EffectiveRate() == TimeCompressionRate::X8, "releasing clamp must restore requested rate"))
    {
        return 1;
    }

    {
        TimeCompressionSafetyScope scope(controller);
        if (!Expect(PublishTimeCompressionSafetyCap(TimeCompressionRate::X4), "bound safety publisher must accept 4x") ||
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
                "publisher outside fixed-update safety scope must be rejected"))
    {
        return 1;
    }

    if (!Expect(controller.SetMaximumRate(TimeCompressionRate::X8), "test must release safety ceiling") ||
        !RunCombatPolicyChecks())
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
        !Expect(TimeCompressionMultiplier(TimeCompressionRate::X8) == 8.0, "8x multiplier contract"))
    {
        return 1;
    }

    std::cout << "Time compression policy: PASS\n";
    return 0;
}
