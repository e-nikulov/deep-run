#pragma once

#include <cstdint>

namespace DeepRun::Core
{
class FixedStepAccumulator final
{
public:
    explicit FixedStepAccumulator(double stepSeconds = 1.0 / 60.0);

    void SetStepSeconds(double stepSeconds);
    void Reset() noexcept;
    [[nodiscard]] std::uint32_t Accumulate(double deltaSeconds) noexcept;

    [[nodiscard]] double StepSeconds() const noexcept;
    [[nodiscard]] double RemainingSeconds() const noexcept;

private:
    double stepSeconds_ = 1.0 / 60.0;
    double accumulatedSeconds_ = 0.0;
};
}
