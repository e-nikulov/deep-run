#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace DeepRun::Render
{
inline constexpr std::size_t GerstnerWaveComponentCapacity = 7U;
inline constexpr std::size_t LegacyM3GerstnerWaveComponentCount = 3U;

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

struct GerstnerSurfaceDrawStats final
{
    std::uint32_t vertexCount = 0U;
    std::uint32_t indexCount = 0U;
    std::uint32_t drawCalls = 0U;
};

inline constexpr std::size_t GerstnerSurfaceTransientDisturbanceCapacity = 3U;

// Generic presentation-only compact height disturbance. The renderer assigns no scene meaning to the source.
// Game supplies at most three bounded impulses and remains the sole owner of any semantic interpretation.
struct GerstnerSurfaceTransientDisturbance final
{
    float centerX = 0.0F;
    float radiusMeters = 0.0F;
    float verticalAmplitudeMeters = 0.0F;
    float profileExponent = 2.0F;
};

[[nodiscard]] std::expected<void, std::string> ValidateGerstnerSurfaceTransientDisturbances(
    std::span<const GerstnerSurfaceTransientDisturbance> disturbances);

[[nodiscard]] std::expected<void, std::string> ValidateGerstnerSurfacePresentationParameters(
    const GerstnerSurfacePresentationParameters& parameters);
[[nodiscard]] std::expected<float, std::string> MaximumGerstnerCombinedVerticalAmplitudeMeters(
    const GerstnerSurfacePresentationParameters& parameters);
[[nodiscard]] std::expected<GerstnerSurfaceBaseMesh, std::string> GenerateGerstnerSurfaceBaseMesh(
    const GerstnerSurfacePresentationParameters& parameters);
[[nodiscard]] std::expected<GerstnerSurfacePresentationPosition, std::string> EvaluateGerstnerSurfacePresentation(
    const GerstnerSurfacePresentationParameters& parameters,
    float x,
    float phaseTimeSeconds);
}
