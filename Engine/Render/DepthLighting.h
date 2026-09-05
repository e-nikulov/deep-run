#pragma once

#include <array>
#include <expected>
#include <string>

namespace DeepRun::Render
{
// Presentation-only scene parameters supplied by Game. Values are scene-linear and apply before SDR/HDR
// output conversion. The generic renderer only consumes this finite value snapshot.
struct DepthLightingParameters final
{
    float surfaceLevelYMeters = 0.0F;
    std::array<float, 3> attenuationPerMeterRgb{};
    std::array<float, 3> deepAmbientRgb{};
};

struct DepthLightingSample final
{
    float depthMeters = 0.0F;
    std::array<float, 3> directTransmissionRgb{1.0F, 1.0F, 1.0F};
    std::array<float, 3> deepAmbientWeightRgb{};
};

// Rejects non-finite values and negative attenuation/ambient tuning. The parameter set is deliberately
// small: it is not a settings system or an optical model.
[[nodiscard]] std::expected<void, std::string> ValidateDepthLightingParameters(const DepthLightingParameters& parameters);

// Evaluates the renderer-neutral depth-lighting math at an actual world-space Y. depth is exactly
// max(surfaceLevelY - worldY, 0), so camera position/distance never participates.
[[nodiscard]] std::expected<DepthLightingSample, std::string> EvaluateDepthLighting(
    const DepthLightingParameters& parameters,
    float worldYMeters);
} // namespace DeepRun::Render
