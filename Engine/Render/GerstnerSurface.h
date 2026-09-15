#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace DeepRun::Render
{
inline constexpr std::size_t GerstnerWaveComponentCapacity = 7U;
inline constexpr std::size_t LegacyM3GerstnerWaveComponentCount = 3U;
inline constexpr std::uint32_t GerstnerMeshletCellCapacity = 31U;
inline constexpr std::uint32_t GerstnerMeshletMinimumCellCount = 256U;
inline constexpr std::uint32_t GerstnerMeshletMaximumCellCount = 8192U;
inline constexpr std::uint32_t GerstnerMeshletVisibleSamplesPerWavelength = 16U;

struct GerstnerWaveComponent final
{
    float amplitudeMeters = 0.0F;
    float wavelengthMeters = 0.0F;
    // W1-B permits signed angular frequency in production snapshots to preserve projected travel direction.
    float angularFrequencyRadiansPerSecond = 0.0F;
    float phaseOffsetRadians = 0.0F;
    float horizontalSteepness = 0.0F;
};

// Renderer-neutral, Game-supplied snapshot. Engine owns only this presentation transport.
// activeComponentCount preserves the accepted three-wave M3 snapshot while allowing Game to supply
// the seven-component W1-B production surface through the same one-draw path.
struct GerstnerSurfacePresentationParameters final
{
    float minimumX = 0.0F;
    float maximumX = 0.0F;
    float referenceLevelY = 0.0F;
    float bottomFillY = 0.0F;
    std::uint32_t horizontalSampleCount = 0U;
    std::array<GerstnerWaveComponent, GerstnerWaveComponentCapacity> components{};
    std::size_t activeComponentCount = LegacyM3GerstnerWaveComponentCount;
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

struct GerstnerMeshletDispatchPlan final
{
    std::uint32_t cellCount = 0U;
    std::uint32_t meshletCount = 0U;
    std::uint32_t emittedVertexCount = 0U;
    std::uint32_t emittedPrimitiveCount = 0U;
};

struct GerstnerSurfaceDrawStats final
{
    std::uint32_t vertexCount = 0U;
    std::uint32_t indexCount = 0U;
    std::uint32_t drawCalls = 0U;
    std::uint32_t meshletCount = 0U;
    bool meshShaderPath = false;
};

[[nodiscard]] std::expected<void, std::string> ValidateGerstnerSurfacePresentationParameters(
    const GerstnerSurfacePresentationParameters& parameters);
[[nodiscard]] std::expected<float, std::string> MaximumGerstnerCombinedVerticalAmplitudeMeters(
    const GerstnerSurfacePresentationParameters& parameters);
[[nodiscard]] std::expected<GerstnerSurfaceBaseMesh, std::string> GenerateGerstnerSurfaceBaseMesh(
    const GerstnerSurfacePresentationParameters& parameters);
[[nodiscard]] std::expected<GerstnerMeshletDispatchPlan, std::string> BuildGerstnerMeshletDispatchPlan(
    const GerstnerSurfacePresentationParameters& parameters,
    float cameraHorizontalSpanMeters,
    std::uint32_t viewportWidthPixels);
[[nodiscard]] std::expected<GerstnerSurfacePresentationPosition, std::string> EvaluateGerstnerSurfacePresentation(
    const GerstnerSurfacePresentationParameters& parameters,
    float x,
    float phaseTimeSeconds);
}
