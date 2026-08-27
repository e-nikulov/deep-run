#include "Engine/Core/Time.h"

#include <algorithm>

namespace DeepRun::Core
{
FrameTimer::FrameTimer()
    : previous_(Clock::now())
{
}

void FrameTimer::Tick()
{
    const Clock::time_point now = Clock::now();
    deltaSeconds_ = std::chrono::duration<double>(now - previous_).count();
    previous_ = now;
    deltaSeconds_ = std::clamp(deltaSeconds_, 0.0, 0.25);
    smoothedDeltaSeconds_ = (smoothedDeltaSeconds_ * 0.9) + (deltaSeconds_ * 0.1);
}

double FrameTimer::DeltaSeconds() const noexcept
{
    return deltaSeconds_;
}

double FrameTimer::FrameMilliseconds() const noexcept
{
    return smoothedDeltaSeconds_ * 1000.0;
}

double FrameTimer::FramesPerSecond() const noexcept
{
    return smoothedDeltaSeconds_ > 0.0 ? 1.0 / smoothedDeltaSeconds_ : 0.0;
}
}
