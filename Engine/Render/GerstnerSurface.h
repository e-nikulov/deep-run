#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace DeepRun::Render
{
struct GerstnerWaveComponent final
{
    float amplitudeMeters = 0.0F;
    float wavelengthMeters = 0.0F;
    float angularFrequencyRadiansPerSecond = 0.0F;
    float phaseOffsetRadians = 0.0F;
    float horizontalSteepness = 0.0F;
};

// Renderer-neutral, Game-owned tuning for the bounded M3-E backdrop. It describes visual presentation only;
// it is not a world-state, physics, or simulation interface.
struct GerstnerSurfacePresentationParameters final
{
    float minimumX = 0.0F;
    float maximumX = 0.0F;
    float referenceLevelY = 0.0F;
    float bottomFillY = 0.0F;
    std::uint32_t horizontalSampleCount = 0U;
    std::array<GerstnerWaveComponent, 3> components{};
    std::array<float, 3> deepFillRgb{};
    std::array<float, 3> surfaceTintRgb{};
};

struct GerstnerSurfaceBaseVertex final
{
    float x = 0.0F;
    float y = 0.0F;
    float surfaceWeight = 0.0F;
};

struct GerstnerSurfaceBaseMesh final
{
    std::vector<GerstnerSurfaceBaseVertex> vertices;
    std::vector<std::uint32_t> indices;
};

struct GerstnerSurfacePresentationPosition final
{
    float x = 0.0F;
    float y = 0.0F;

    [[nodiscard]] bool operator==(const GerstnerSurfacePresentationPosition&) const noexcept = default;
};

struct GerstnerSurfaceDrawStats final
{
    std::uint32_t vertexCount = 0U;
    std::uint32_t indexCount = 0U;
    std::uint32_t drawCalls = 0U;
};

[[nodiscard]] std::expected<void, std::string> ValidateGerstnerSurfacePresentationParameters(
    const GerstnerSurfacePresentationParameters& parameters);
[[nodiscard]] std::expected<float, std::string> MaximumGerstnerCombinedVerticalAmplitudeMeters(
    const GerstnerSurfacePresentationParameters& parameters);
[[nodiscard]] std::expected<GerstnerSurfaceBaseMesh, std::string> GenerateGerstnerSurfaceBaseMesh(
    const GerstnerSurfacePresentationParameters& parameters);
// Evaluates only the M3-E visual presentation formula from PresentationTime for renderer validation/tests.
// This API supplies no authoritative state and must not drive Simulation, force/rigid-body, sonar, or gameplay
// depth consumers. M3-E.1 will introduce a separate Simulation-owned wave-definition/query contract.
[[nodiscard]] std::expected<GerstnerSurfacePresentationPosition, std::string> EvaluateGerstnerSurfacePresentation(
    const GerstnerSurfacePresentationParameters& parameters,
    float x,
    float presentationTimeSeconds);
}
