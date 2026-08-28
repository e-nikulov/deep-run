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
    const double deltaSeconds = std::chrono::duration<double>(now - previous_).count();
    previous_ = now;
    Advance(deltaSeconds);
}

void FrameTimer::Advance(const double deltaSeconds)
{
    state_.deltaSeconds = std::clamp(deltaSeconds, 0.0, 0.25);
    state_.elapsedSeconds += state_.deltaSeconds;
    ++state_.frameIndex;
    smoothedDeltaSeconds_ = (smoothedDeltaSeconds_ * 0.9) + (state_.deltaSeconds * 0.1);
}

void FrameTimer::Reset()
{
    state_ = {};
    smoothedDeltaSeconds_ = 1.0 / 60.0;
    Rebase();
}

void FrameTimer::Rebase() noexcept
{
    previous_ = Clock::now();
    state_.deltaSeconds = 0.0;
}

double FrameTimer::DeltaSeconds() const noexcept
{
    return state_.deltaSeconds;
}

double FrameTimer::ElapsedSeconds() const noexcept
{
    return state_.elapsedSeconds;
}

std::uint64_t FrameTimer::FrameIndex() const noexcept
{
    return state_.frameIndex;
}

const FrameState& FrameTimer::State() const noexcept
{
    return state_;
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
