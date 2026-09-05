#include "Engine/Render/DepthLighting.h"

#include <algorithm>
#include <cmath>

namespace DeepRun::Render
{
std::expected<void, std::string> ValidateDepthLightingParameters(const DepthLightingParameters& parameters)
{
    if (!std::isfinite(parameters.surfaceLevelYMeters))
    {
        return std::unexpected("depth-light surface level must be finite");
    }
    for (std::size_t channel = 0; channel < 3U; ++channel)
    {
        if (!std::isfinite(parameters.attenuationPerMeterRgb[channel]) ||
            parameters.attenuationPerMeterRgb[channel] < 0.0F)
        {
            return std::unexpected("depth-light attenuation must be finite and non-negative");
        }
        if (!std::isfinite(parameters.deepAmbientRgb[channel]) || parameters.deepAmbientRgb[channel] < 0.0F)
        {
            return std::unexpected("depth-light ambient must be finite and non-negative");
        }
    }
    return {};
}

std::expected<DepthLightingSample, std::string> EvaluateDepthLighting(
    const DepthLightingParameters& parameters,
    const float worldYMeters)
{
    if (const auto valid = ValidateDepthLightingParameters(parameters); !valid)
    {
        return std::unexpected(valid.error());
    }
    if (!std::isfinite(worldYMeters))
    {
        return std::unexpected("depth-light world Y must be finite");
    }

    DepthLightingSample result;
    result.depthMeters = std::max(parameters.surfaceLevelYMeters - worldYMeters, 0.0F);
    if (!std::isfinite(result.depthMeters))
    {
        return std::unexpected("depth-light derived depth must be finite");
    }
    for (std::size_t channel = 0; channel < 3U; ++channel)
    {
        result.directTransmissionRgb[channel] = std::exp(-parameters.attenuationPerMeterRgb[channel] * result.depthMeters);
        result.deepAmbientWeightRgb[channel] = 1.0F - result.directTransmissionRgb[channel];
    }
    return result;
}
} // namespace DeepRun::Render
