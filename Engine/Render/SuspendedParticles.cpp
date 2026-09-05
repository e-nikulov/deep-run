#include "Engine/Render/SuspendedParticles.h"

#include <cmath>
#include <limits>

namespace DeepRun::Render
{
namespace
{
constexpr std::uint32_t MaximumParticleCount = 512U;
constexpr float TwoPi = 6.28318530717958647692F;

[[nodiscard]] bool IsFiniteVector(const std::array<float, 3>& value) noexcept
{
    return std::isfinite(value[0]) && std::isfinite(value[1]) && std::isfinite(value[2]);
}

[[nodiscard]] std::uint32_t NextRandom(std::uint32_t& state) noexcept
{
    // Xorshift32 is sufficient for fixed visual placement and does not depend on a platform RNG or clock.
    state ^= state << 13U;
    state ^= state >> 17U;
    state ^= state << 5U;
    return state;
}

[[nodiscard]] float UnitRandom(std::uint32_t& state) noexcept
{
    constexpr float InverseUint32Range = 1.0F / 4'294'967'295.0F;
    return static_cast<float>(NextRandom(state)) * InverseUint32Range;
}

[[nodiscard]] float Wrap(const float value, const float minimum, const float maximum) noexcept
{
    const float span = maximum - minimum;
    float wrapped = std::fmod(value - minimum, span);
    if (wrapped < 0.0F)
    {
        wrapped += span;
    }
    return minimum + wrapped;
}
} // namespace

std::expected<void, std::string> ValidateSuspendedParticleFieldParameters(
    const SuspendedParticleFieldParameters& parameters)
{
    if (parameters.particleCount == 0U || parameters.particleCount > MaximumParticleCount)
    {
        return std::unexpected("suspended particle count must be between 1 and 512");
    }
    if (!IsFiniteVector(parameters.minimumWorldPosition) || !IsFiniteVector(parameters.maximumWorldPosition))
    {
        return std::unexpected("suspended particle field bounds must be finite");
    }
    for (std::size_t axis = 0; axis < parameters.minimumWorldPosition.size(); ++axis)
    {
        if (parameters.maximumWorldPosition[axis] <= parameters.minimumWorldPosition[axis])
        {
            return std::unexpected("suspended particle field maximum bounds must exceed minimum bounds");
        }
    }
    if (!std::isfinite(parameters.particleSizeMeters) || parameters.particleSizeMeters <= 0.0F)
    {
        return std::unexpected("suspended particle size must be finite and positive");
    }
    if (!std::isfinite(parameters.particleOpacity) || parameters.particleOpacity <= 0.0F ||
        parameters.particleOpacity > 1.0F)
    {
        return std::unexpected("suspended particle opacity must be finite and in (0, 1]");
    }
    if (!std::isfinite(parameters.verticalDriftMetersPerSecond) || parameters.verticalDriftMetersPerSecond <= 0.0F)
    {
        return std::unexpected("suspended particle vertical drift must be finite and positive");
    }
    if (!std::isfinite(parameters.lateralOscillationAmplitudeMeters) ||
        parameters.lateralOscillationAmplitudeMeters < 0.0F ||
        parameters.lateralOscillationAmplitudeMeters * 2.0F >=
            parameters.maximumWorldPosition[0] - parameters.minimumWorldPosition[0])
    {
        return std::unexpected("suspended particle lateral amplitude must fit inside the field bounds");
    }
    if (!std::isfinite(parameters.lateralOscillationAngularFrequency) ||
        parameters.lateralOscillationAngularFrequency < 0.0F)
    {
        return std::unexpected("suspended particle lateral frequency must be finite and non-negative");
    }
    return {};
}

std::expected<std::vector<SuspendedParticle>, std::string> GenerateSuspendedParticleField(
    const SuspendedParticleFieldParameters& parameters)
{
    if (const auto valid = ValidateSuspendedParticleFieldParameters(parameters); !valid)
    {
        return std::unexpected(valid.error());
    }

    std::uint32_t state = parameters.seed == 0U ? 0xA341316CU : parameters.seed;
    const float minimumX = parameters.minimumWorldPosition[0] + parameters.lateralOscillationAmplitudeMeters;
    const float maximumX = parameters.maximumWorldPosition[0] - parameters.lateralOscillationAmplitudeMeters;
    std::vector<SuspendedParticle> particles;
    particles.reserve(parameters.particleCount);
    for (std::uint32_t index = 0; index < parameters.particleCount; ++index)
    {
        particles.push_back({
            .initialWorldPosition = {
                minimumX + (maximumX - minimumX) * UnitRandom(state),
                parameters.minimumWorldPosition[1] +
                    (parameters.maximumWorldPosition[1] - parameters.minimumWorldPosition[1]) * UnitRandom(state),
                parameters.minimumWorldPosition[2] +
                    (parameters.maximumWorldPosition[2] - parameters.minimumWorldPosition[2]) * UnitRandom(state)},
            .phaseRadians = TwoPi * UnitRandom(state)});
    }
    return particles;
}

std::expected<std::array<float, 3>, std::string> EvaluateSuspendedParticlePosition(
    const SuspendedParticleFieldParameters& parameters,
    const SuspendedParticle& particle,
    const float presentationTimeSeconds)
{
    if (const auto valid = ValidateSuspendedParticleFieldParameters(parameters); !valid)
    {
        return std::unexpected(valid.error());
    }
    if (!IsFiniteVector(particle.initialWorldPosition) || !std::isfinite(particle.phaseRadians) ||
        !std::isfinite(presentationTimeSeconds) || presentationTimeSeconds < 0.0F)
    {
        return std::unexpected("suspended particle animation inputs must be finite and presentation time non-negative");
    }

    const float animatedX = particle.initialWorldPosition[0] +
                            std::sin(particle.phaseRadians +
                                     presentationTimeSeconds * parameters.lateralOscillationAngularFrequency) *
                                parameters.lateralOscillationAmplitudeMeters;
    const float animatedY = Wrap(
        particle.initialWorldPosition[1] + presentationTimeSeconds * parameters.verticalDriftMetersPerSecond,
        parameters.minimumWorldPosition[1],
        parameters.maximumWorldPosition[1]);
    const std::array<float, 3> position{animatedX, animatedY, particle.initialWorldPosition[2]};
    if (!IsFiniteVector(position) || position[0] < parameters.minimumWorldPosition[0] ||
        position[0] > parameters.maximumWorldPosition[0] || position[1] < parameters.minimumWorldPosition[1] ||
        position[1] > parameters.maximumWorldPosition[1] || position[2] < parameters.minimumWorldPosition[2] ||
        position[2] > parameters.maximumWorldPosition[2])
    {
        return std::unexpected("suspended particle animation produced a position outside the field bounds");
    }
    return position;
}

std::expected<float, std::string> SuspendedParticleVerticalWrapPeriodSeconds(
    const SuspendedParticleFieldParameters& parameters)
{
    if (const auto valid = ValidateSuspendedParticleFieldParameters(parameters); !valid)
    {
        return std::unexpected(valid.error());
    }
    const float period =
        (parameters.maximumWorldPosition[1] - parameters.minimumWorldPosition[1]) / parameters.verticalDriftMetersPerSecond;
    if (!std::isfinite(period) || period <= 0.0F || period >= std::numeric_limits<float>::max())
    {
        return std::unexpected("suspended particle vertical wrap period must be finite and positive");
    }
    return period;
}
} // namespace DeepRun::Render
