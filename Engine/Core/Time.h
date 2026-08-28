#pragma once

#include <chrono>
#include <cstdint>

namespace DeepRun::Core
{
struct FrameState final
{
    double deltaSeconds = 0.0;
    double elapsedSeconds = 0.0;
    std::uint64_t frameIndex = 0;
};

class FrameTimer final
{
public:
    FrameTimer();
    void Tick();
    void Advance(double deltaSeconds);
    void Reset();
    void Rebase() noexcept;

    [[nodiscard]] double DeltaSeconds() const noexcept;
    [[nodiscard]] double ElapsedSeconds() const noexcept;
    [[nodiscard]] std::uint64_t FrameIndex() const noexcept;
    [[nodiscard]] const FrameState& State() const noexcept;
    [[nodiscard]] double FrameMilliseconds() const noexcept;
    [[nodiscard]] double FramesPerSecond() const noexcept;

private:
    using Clock = std::chrono::steady_clock;
    Clock::time_point previous_;
    FrameState state_;
    double smoothedDeltaSeconds_ = 1.0 / 60.0;
};
}
