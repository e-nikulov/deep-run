#include "Engine/Render/GerstnerSurface.h"

#include <cmath>
#include <limits>

namespace DeepRun::Render
{
namespace
{
constexpr float TwoPi = 6.28318530717958647692F;
constexpr std::uint32_t MinimumHorizontalSampleCount = 2U;
constexpr std::uint32_t MaximumHorizontalSampleCount = 513U;
constexpr float MaximumComponentAmplitudeMeters = 3.0F;
constexpr float MaximumCombinedVerticalAmplitudeMeters = 4.0F;
constexpr float MaximumConservativeHorizontalSlope = 0.5F;

[[nodiscard]] bool IsFiniteColor(const std::array<float, 3>& color) noexcept
{
    for (const float channel : color)
    {
        if (!std::isfinite(channel) || channel < 0.0F || channel > 1.0F)
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool IsFiniteComponent(const GerstnerWaveComponent& component) noexcept
{
    return std::isfinite(component.amplitudeMeters) && std::isfinite(component.wavelengthMeters) &&
           std::isfinite(component.angularFrequencyRadiansPerSecond) && std::isfinite(component.phaseOffsetRadians) &&
           std::isfinite(component.horizontalSteepness);
}
} // namespace

std::expected<void, std::string> ValidateGerstnerSurfacePresentationParameters(
    const GerstnerSurfacePresentationParameters& parameters)
{
    if (!std::isfinite(parameters.minimumX) || !std::isfinite(parameters.maximumX) ||
        !std::isfinite(parameters.referenceLevelY) || !std::isfinite(parameters.bottomFillY))
    {
        return std::unexpected("Gerstner surface bounds must be finite");
    }
    if (parameters.minimumX >= parameters.maximumX)
    {
        return std::unexpected("Gerstner surface horizontal bounds must be ordered");
    }
    if (parameters.horizontalSampleCount < MinimumHorizontalSampleCount ||
        parameters.horizontalSampleCount > MaximumHorizontalSampleCount)
    {
        return std::unexpected("Gerstner surface sample count is outside the bounded range");
    }
    if (!IsFiniteColor(parameters.deepFillRgb) || !IsFiniteColor(parameters.surfaceTintRgb))
    {
        return std::unexpected("Gerstner surface colors must be finite SDR-normalized RGB values");
    }

    float combinedVerticalAmplitude = 0.0F;
    float conservativeHorizontalSlope = 0.0F;
    for (const GerstnerWaveComponent& component : parameters.components)
    {
        if (!IsFiniteComponent(component))
        {
            return std::unexpected("Gerstner surface component values must be finite");
        }
        if (component.amplitudeMeters <= 0.0F || component.amplitudeMeters > MaximumComponentAmplitudeMeters ||
            component.wavelengthMeters <= 0.0F || component.angularFrequencyRadiansPerSecond <= 0.0F ||
            component.horizontalSteepness < 0.0F || component.horizontalSteepness > 1.0F)
        {
            return std::unexpected("Gerstner surface component is outside the restrained presentation range");
        }

        const float waveNumber = TwoPi / component.wavelengthMeters;
        if (!std::isfinite(waveNumber) || waveNumber <= 0.0F)
        {
            return std::unexpected("Gerstner surface wavelength is invalid");
        }
        combinedVerticalAmplitude += component.amplitudeMeters;
        conservativeHorizontalSlope += component.horizontalSteepness * component.amplitudeMeters * waveNumber;
    }
    if (!std::isfinite(combinedVerticalAmplitude) ||
        combinedVerticalAmplitude > MaximumCombinedVerticalAmplitudeMeters)
    {
        return std::unexpected("Gerstner surface combined vertical amplitude is outside the restrained range");
    }
    if (!std::isfinite(conservativeHorizontalSlope) || conservativeHorizontalSlope >= MaximumConservativeHorizontalSlope)
    {
        return std::unexpected("Gerstner surface conservative horizontal foldover bound failed");
    }
    if (parameters.bottomFillY >= parameters.referenceLevelY - combinedVerticalAmplitude)
    {
        return std::unexpected("Gerstner surface bottom fill must remain below every possible trough");
    }
    return {};
}

std::expected<float, std::string> MaximumGerstnerCombinedVerticalAmplitudeMeters(
    const GerstnerSurfacePresentationParameters& parameters)
{
    if (const auto valid = ValidateGerstnerSurfacePresentationParameters(parameters); !valid)
    {
        return std::unexpected(valid.error());
    }

    float result = 0.0F;
    for (const GerstnerWaveComponent& component : parameters.components)
    {
        result += component.amplitudeMeters;
    }
    return result;
}

std::expected<GerstnerSurfaceBaseMesh, std::string> GenerateGerstnerSurfaceBaseMesh(
    const GerstnerSurfacePresentationParameters& parameters)
{
    if (const auto valid = ValidateGerstnerSurfacePresentationParameters(parameters); !valid)
    {
        return std::unexpected(valid.error());
    }

    GerstnerSurfaceBaseMesh result;
    result.vertices.reserve(static_cast<std::size_t>(parameters.horizontalSampleCount) * 2U);
    result.indices.reserve(static_cast<std::size_t>(parameters.horizontalSampleCount - 1U) * 6U);
    const float span = parameters.maximumX - parameters.minimumX;
    const float denominator = static_cast<float>(parameters.horizontalSampleCount - 1U);
    for (std::uint32_t sample = 0U; sample < parameters.horizontalSampleCount; ++sample)
    {
        const float normalizedX = static_cast<float>(sample) / denominator;
        const float x = parameters.minimumX + span * normalizedX;
        result.vertices.push_back({.x = x, .y = parameters.referenceLevelY, .surfaceWeight = 1.0F});
        result.vertices.push_back({.x = x, .y = parameters.bottomFillY, .surfaceWeight = 0.0F});
    }
    for (std::uint32_t cell = 0U; cell + 1U < parameters.horizontalSampleCount; ++cell)
    {
        const std::uint32_t upperLeft = cell * 2U;
        const std::uint32_t lowerLeft = upperLeft + 1U;
        const std::uint32_t upperRight = upperLeft + 2U;
        const std::uint32_t lowerRight = upperLeft + 3U;
        result.indices.insert(result.indices.end(), {upperLeft, lowerLeft, upperRight, upperRight, lowerLeft, lowerRight});
    }
    return result;
}

std::expected<GerstnerSurfacePresentationPosition, std::string> EvaluateGerstnerSurfacePresentation(
    const GerstnerSurfacePresentationParameters& parameters,
    const float x,
    const float phaseTimeSeconds)
{
    if (const auto valid = ValidateGerstnerSurfacePresentationParameters(parameters); !valid)
    {
        return std::unexpected(valid.error());
    }
    if (!std::isfinite(x) || !std::isfinite(phaseTimeSeconds) || phaseTimeSeconds < 0.0F)
    {
        return std::unexpected("Gerstner surface presentation input must be finite and non-negative in time");
    }

    double displacedX = static_cast<double>(x);
    double displacedY = static_cast<double>(parameters.referenceLevelY);
    for (const GerstnerWaveComponent& component : parameters.components)
    {
        const double waveNumber = static_cast<double>(TwoPi) / static_cast<double>(component.wavelengthMeters);
        const double theta = waveNumber * static_cast<double>(x) -
                             static_cast<double>(component.angularFrequencyRadiansPerSecond) *
                                 static_cast<double>(phaseTimeSeconds) +
                             static_cast<double>(component.phaseOffsetRadians);
        displacedX += static_cast<double>(component.horizontalSteepness) *
                      static_cast<double>(component.amplitudeMeters) * std::cos(theta);
        displacedY += static_cast<double>(component.amplitudeMeters) * std::sin(theta);
    }
    if (!std::isfinite(displacedX) || !std::isfinite(displacedY) ||
        std::abs(displacedX) > static_cast<double>((std::numeric_limits<float>::max)()) ||
        std::abs(displacedY) > static_cast<double>((std::numeric_limits<float>::max)()))
    {
        return std::unexpected("Gerstner surface presentation evaluation became non-finite");
    }
    return GerstnerSurfacePresentationPosition{
        .x = static_cast<float>(displacedX),
        .y = static_cast<float>(displacedY)};
}
} // namespace DeepRun::Render
