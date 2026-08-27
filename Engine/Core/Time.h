#pragma once

#include <chrono>

namespace DeepRun::Core
{
class FrameTimer final
{
public:
    FrameTimer();
    void Tick();

    [[nodiscard]] double DeltaSeconds() const noexcept;
    [[nodiscard]] double FrameMilliseconds() const noexcept;
    [[nodiscard]] double FramesPerSecond() const noexcept;

private:
    using Clock = std::chrono::steady_clock;
    Clock::time_point previous_;
    double deltaSeconds_ = 1.0 / 60.0;
    double smoothedDeltaSeconds_ = 1.0 / 60.0;
};
}
