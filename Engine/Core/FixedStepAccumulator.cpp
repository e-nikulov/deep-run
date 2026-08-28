#include "Engine/Core/FixedStepAccumulator.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>

namespace DeepRun::Core
{
FixedStepAccumulator::FixedStepAccumulator(const double stepSeconds)
{
    SetStepSeconds(stepSeconds);
}

void FixedStepAccumulator::SetStepSeconds(const double stepSeconds)
{
    assert(std::isfinite(stepSeconds) && stepSeconds > 0.0);
    stepSeconds_ = stepSeconds;
    Reset();
}

void FixedStepAccumulator::Reset() noexcept
{
    accumulatedSeconds_ = 0.0;
}

std::uint32_t FixedStepAccumulator::Accumulate(const double deltaSeconds) noexcept
{
    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0)
    {
        return 0;
    }

    accumulatedSeconds_ += deltaSeconds;
    const double tolerance = stepSeconds_ * 1.0e-9;
    const double stepCount = std::floor((accumulatedSeconds_ + tolerance) / stepSeconds_);
    const double cappedStepCount = std::min(
        stepCount,
        static_cast<double>(std::numeric_limits<std::uint32_t>::max()));
    const auto steps = static_cast<std::uint32_t>(cappedStepCount);
    accumulatedSeconds_ -= static_cast<double>(steps) * stepSeconds_;
    if (accumulatedSeconds_ < 0.0 && accumulatedSeconds_ >= -tolerance)
    {
        accumulatedSeconds_ = 0.0;
    }
    return steps;
}

double FixedStepAccumulator::StepSeconds() const noexcept
{
    return stepSeconds_;
}

double FixedStepAccumulator::RemainingSeconds() const noexcept
{
    return accumulatedSeconds_;
}
}
