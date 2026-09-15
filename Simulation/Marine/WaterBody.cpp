#include "Simulation/Marine/WaterBody.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <numeric>

namespace DeepRun::Marine
{
namespace
{
constexpr Physics::PhysicsVector3 kSurfaceNormal = {0.0F, 1.0F, 0.0F};
constexpr float MaximumWaveComponentAmplitudeMeters = 3.0F;
constexpr double LegacyMaximumCombinedVerticalAmplitudeMeters = 4.0;
constexpr double ProductionMaximumCombinedVerticalAmplitudeMeters = 20.0;
constexpr double MaximumConservativeHorizontalSlope = 0.5;

WaterBodyError MakeConfigError(const std::string& message)
{
    return WaterBodyError{WaterBodyErrorCode::InvalidConfiguration, message};
}
} // namespace

std::expected<WaterBody, WaterBodyError> WaterBody::Create(const WaterBodyConfig& config)
{
    if (!std::isfinite(config.surfaceLevelY))
    {
        return std::unexpected(MakeConfigError("surfaceLevelY must be finite"));
    }
    if (!std::isfinite(config.densityKgPerCubicMeter) || config.densityKgPerCubicMeter <= 0.0F)
    {
        return std::unexpected(
            MakeConfigError("densityKgPerCubicMeter must be finite and strictly positive"));
    }
    if (config.waves)
    {
        const std::size_t activeCount = config.waves->activeComponentCount;
        if (activeCount == 0U || activeCount > config.waves->components.size())
        {
            return std::unexpected(MakeConfigError("wave active component count is outside the bounded range"));
        }

        const bool legacyContract = activeCount <= LegacyM3WaterWaveComponentCount;
        double amplitude = 0.0;
        double slope = 0.0;
        for (std::size_t index = 0U; index < activeCount; ++index)
        {
            const auto& wave = config.waves->components[index];
            const bool validFrequency = legacyContract
                ? wave.angularFrequencyRadiansPerSecond > 0.0F
                : std::abs(wave.angularFrequencyRadiansPerSecond) > 0.0F;
            if (!std::isfinite(wave.amplitudeMeters) || wave.amplitudeMeters <= 0.0F ||
                wave.amplitudeMeters > MaximumWaveComponentAmplitudeMeters ||
                !std::isfinite(wave.wavelengthMeters) || wave.wavelengthMeters <= 0.0F ||
                !std::isfinite(wave.angularFrequencyRadiansPerSecond) || !validFrequency ||
                !std::isfinite(wave.phaseOffsetRadians) || !std::isfinite(wave.horizontalSteepness) ||
                wave.horizontalSteepness < 0.0F || wave.horizontalSteepness > 1.0F)
            {
                return std::unexpected(MakeConfigError("invalid wave component"));
            }
            amplitude += wave.amplitudeMeters;
            slope += static_cast<double>(wave.horizontalSteepness) * wave.amplitudeMeters *
                     (2.0 * std::numbers::pi / wave.wavelengthMeters);
        }
        const double maximumCombinedAmplitude = legacyContract
            ? LegacyMaximumCombinedVerticalAmplitudeMeters
            : ProductionMaximumCombinedVerticalAmplitudeMeters;
        if (amplitude > maximumCombinedAmplitude || !std::isfinite(slope) ||
            slope >= MaximumConservativeHorizontalSlope)
        {
            return std::unexpected(MakeConfigError("wave amplitude or non-folding bound exceeded"));
        }
    }
    return WaterBody{config};
}

WaterBody::WaterBody(const WaterBodyConfig config) : config_(config) {}

std::expected<WaterSurfaceSample, WaterBodyError> WaterBody::SampleWaveSurface(
    const Physics::PhysicsVector3& position, const double simulationTimeSeconds) const
{
    if (!position.IsFinite())
    {
        return std::unexpected(WaterBodyError{WaterBodyErrorCode::InvalidQueryPosition, "position must be finite"});
    }
    if (!std::isfinite(simulationTimeSeconds) || simulationTimeSeconds < 0.0)
    {
        return std::unexpected(WaterBodyError{WaterBodyErrorCode::InvalidQueryTime, "SimulationTime must be finite and non-negative"});
    }
    if (!config_.waves)
    {
        return Sample(position);
    }

    const std::size_t activeCount = config_.waves->activeComponentCount;
    double horizontalBound = 0.0;
    for (std::size_t index = 0U; index < activeCount; ++index)
    {
        const auto& wave = config_.waves->components[index];
        horizontalBound += static_cast<double>(wave.horizontalSteepness) * wave.amplitudeMeters;
    }
    const auto evaluate = [&](const double u)
    {
        std::array<double, 4> result{u, config_.surfaceLevelY, 1.0, 0.0};
        for (std::size_t index = 0U; index < activeCount; ++index)
        {
            const auto& wave = config_.waves->components[index];
            const double k = 2.0 * std::numbers::pi / wave.wavelengthMeters;
            const double theta = k * u - static_cast<double>(wave.angularFrequencyRadiansPerSecond) * simulationTimeSeconds +
                                 wave.phaseOffsetRadians;
            const double sine = std::sin(theta);
            const double cosine = std::cos(theta);
            const double horizontal = static_cast<double>(wave.horizontalSteepness) * wave.amplitudeMeters;
            result[0] += horizontal * cosine;
            result[1] += static_cast<double>(wave.amplitudeMeters) * sine;
            result[2] -= horizontal * k * sine;
            result[3] += static_cast<double>(wave.amplitudeMeters) * k * cosine;
        }
        return result;
    };
    double low = static_cast<double>(position.x) - horizontalBound;
    double high = static_cast<double>(position.x) + horizontalBound;
    for (int iteration = 0; iteration < 48; ++iteration)
    {
        const double middle = std::midpoint(low, high);
        if (evaluate(middle)[0] < position.x)
        {
            low = middle;
        }
        else
        {
            high = middle;
        }
    }
    const auto result = evaluate(std::midpoint(low, high));
    const double length = std::hypot(result[2], result[3]);
    const double depth = result[1] - static_cast<double>(position.y);
    const double limit = (std::numeric_limits<float>::max)();
    if (!std::isfinite(result[0]) || !std::isfinite(result[1]) || !std::isfinite(length) ||
        result[2] <= 0.0 || length <= 0.0 || !std::isfinite(depth) ||
        std::abs(result[1]) > limit || std::abs(depth) > limit)
    {
        return std::unexpected(WaterBodyError{WaterBodyErrorCode::NonFiniteResult, "wave sample is not finite/representable"});
    }
    const WaterSurfaceSample sample{
        .surfaceLevelY = static_cast<float>(result[1]),
        .surfaceNormal = {static_cast<float>(-result[3] / length), static_cast<float>(result[2] / length), 0.0F},
        .signedDepthMeters = static_cast<float>(depth)};
    if (sample.surfaceNormal.y <= 0.0F)
    {
        return std::unexpected(WaterBodyError{WaterBodyErrorCode::NonFiniteResult, "upward normal is not representable"});
    }
    return sample;
}

const WaterBodyConfig& WaterBody::Config() const noexcept
{
    return config_;
}

std::expected<WaterSurfaceSample, WaterBodyError> WaterBody::Sample(
    const Physics::PhysicsVector3& worldPosition) const
{
    if (!worldPosition.IsFinite())
    {
        return std::unexpected(WaterBodyError{
            WaterBodyErrorCode::InvalidQueryPosition, "world position must be finite"});
    }

    const float signedDepthMeters = config_.surfaceLevelY - worldPosition.y;

    return WaterSurfaceSample{
        .surfaceLevelY = config_.surfaceLevelY,
        .surfaceNormal = kSurfaceNormal,
        .signedDepthMeters = signedDepthMeters};
}
}
