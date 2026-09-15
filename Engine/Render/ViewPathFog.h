#pragma once

#include "Engine/Render/DepthLighting.h"

#include <array>
#include <expected>
#include <string>

namespace DeepRun::Render
{
// The complete small, Game-derived presentation snapshot for one frame. It is renderer-neutral: Game maps
// its authoritative surface meaning into this finite value before any GPU resource is updated.
struct ScenePresentationParameters final
{
    DepthLightingParameters depthLighting{};
    std::array<float, 3> cameraPlaneCenterWorldPosition{};
    std::array<float, 3> cameraViewDirection{};
    float fogExtinctionPerMeter = 0.0F;
    std::array<float, 3> fogColorRgb{};
    float atmosphereExtinctionPerMeter = 0.0F;
    std::array<float, 3> atmosphereFogColorRgb{0.390F, 0.550F, 0.720F};
    float cloudCoverFraction = 0.0F;
    float precipitationFraction = 0.0F;
    float sunTransmittance = 1.0F;
    float skyLuminanceMultiplier = 1.0F;
    float horizonHazeFraction = 0.0F;
    float cloudAdvection = 0.0F;
    float atmosphereBoundaryViewportY = 0.0F;
    float cloudPatternOffset = 0.0F;
    // W1-G optical-only lightning parameters. Thunder timing remains a Game semantic cue and does not enter Render.
    float lightningFlashIntensity = 0.0F;
    float lightningViewportX = 0.5F;
    float lightningPatternOffset = 0.0F;
};

struct ViewPathFogSample final
{
    float submergedPathLengthMeters = 0.0F;
    float transmission = 1.0F;
};

// Validates the frame-scoped presentation snapshot. This is deliberately not a material or settings system.
[[nodiscard]] std::expected<void, std::string> ValidateScenePresentationParameters(
    const ScenePresentationParameters& parameters);

// Reconstructs the finite per-fragment orthographic view ray from its camera-plane origin to the fragment,
// clips only that ray below the supplied horizontal plane, then applies scalar Beer-Lambert extinction.
// The calculation is independent of depth-light transmission.
[[nodiscard]] std::expected<ViewPathFogSample, std::string> EvaluateViewPathFog(
    const ScenePresentationParameters& parameters,
    const std::array<float, 3>& fragmentWorldPosition);
} // namespace DeepRun::Render
