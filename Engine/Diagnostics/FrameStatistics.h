#pragma once

#include <algorithm>
#include <cmath>
#include <optional>
#include <span>

namespace DeepRun::Diagnostics
{
struct FrameStatistics final
{
    double median = 0.0;
    double p95 = 0.0;
    double p99 = 0.0;
    double maximum = 0.0;
};

// Sorts caller-owned bounded storage after measurement. Linear interpolation at (N-1)*p.
[[nodiscard]] inline std::optional<FrameStatistics> SummarizeFrameSamples(std::span<double> samples)
{
    if (samples.empty() || std::ranges::any_of(samples, [](double value) {
            return !std::isfinite(value) || value < 0.0;
        })) return std::nullopt;
    std::ranges::sort(samples);
    const auto percentile = [&](double fraction) {
        const double position = static_cast<double>(samples.size() - 1U) * fraction;
        const auto lower = static_cast<std::size_t>(position);
        const auto upper = std::min(lower + 1U, samples.size() - 1U);
        return std::lerp(samples[lower], samples[upper], position - static_cast<double>(lower));
    };
    return FrameStatistics{percentile(0.5), percentile(0.95), percentile(0.99), samples.back()};
}
}
