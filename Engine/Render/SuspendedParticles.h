#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace DeepRun::Render
{
// A bounded presentation description for one suspended-particle field. It is deliberately not an emitter,
// simulation component, or effect graph. Game supplies one value; the renderer owns its generated GPU state.
struct SuspendedParticleFieldParameters final
{
    std::uint32_t seed = 0U;
    std::uint32_t particleCount = 0U;
    std::array<float, 3> minimumWorldPosition{};
    std::array<float, 3> maximumWorldPosition{};
    float particleSizeMeters = 0.0F;
    float particleOpacity = 0.0F;
    float verticalDriftMetersPerSecond = 0.0F;
    float lateralOscillationAmplitudeMeters = 0.0F;
    float lateralOscillationAngularFrequency = 0.0F;
};

struct SuspendedParticle final
{
    std::array<float, 3> initialWorldPosition{};
    float phaseRadians = 0.0F;
};

struct SuspendedParticleDrawStats final
{
    std::uint32_t particleCount = 0U;
    std::uint32_t vertexCount = 0U;
    std::uint32_t indexCount = 0U;
    std::uint32_t drawCalls = 0U;
};

// The renderer-neutral field math makes deterministic placement and wrapping testable without D3D12.
[[nodiscard]] std::expected<void, std::string> ValidateSuspendedParticleFieldParameters(
    const SuspendedParticleFieldParameters& parameters);
[[nodiscard]] std::expected<std::vector<SuspendedParticle>, std::string> GenerateSuspendedParticleField(
    const SuspendedParticleFieldParameters& parameters);
[[nodiscard]] std::expected<std::array<float, 3>, std::string> EvaluateSuspendedParticlePosition(
    const SuspendedParticleFieldParameters& parameters,
    const SuspendedParticle& particle,
    float presentationTimeSeconds);
[[nodiscard]] std::expected<float, std::string> SuspendedParticleVerticalWrapPeriodSeconds(
    const SuspendedParticleFieldParameters& parameters);
} // namespace DeepRun::Render
