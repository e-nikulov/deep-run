#include "Engine/Core/FixedStepAccumulator.h"
#include "Engine/Core/TimeCompression.h"

#include <iostream>

namespace
{
using DeepRun::Core::FixedStepAccumulator;
using DeepRun::Core::TimeCompressionController;
using DeepRun::Core::TimeCompressionMultiplier;
using DeepRun::Core::TimeCompressionRate;

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

    controller.BreakToRealtime();
    if (!Expect(controller.RequestedRate() == TimeCompressionRate::X1, "combat break must reset player intent to 1x") ||
        !Expect(controller.EffectiveRate() == TimeCompressionRate::X1, "combat break must become effective immediately"))
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
        !Expect(TimeCompressionMultiplier(TimeCompressionRate::X8) == 8.0, "8x multiplier contract"))
    {
        return 1;
    }

    std::cout << "Time compression policy: PASS\n";
    return 0;
}
