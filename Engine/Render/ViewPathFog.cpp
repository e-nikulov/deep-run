#include "Engine/Render/ViewPathFog.h"

#include <algorithm>
#include <cmath>

namespace DeepRun::Render
{
namespace
{
bool IsFiniteVector(const std::array<float, 3>& value) noexcept
{
    return std::isfinite(value[0]) && std::isfinite(value[1]) && std::isfinite(value[2]);
}

std::expected<std::array<float, 3>, std::string> NormalizeViewDirection(const std::array<float, 3>& value)
{
    const float length = std::hypot(value[0], value[1], value[2]);
    if (!std::isfinite(length) || length <= 1.0e-6F)
    {
        return std::unexpected("scene presentation camera view direction must be finite and non-zero");
    }
    return std::array<float, 3>{value[0] / length, value[1] / length, value[2] / length};
}
}

std::expected<void, std::string> ValidateScenePresentationParameters(
    const ScenePresentationParameters& parameters)
{
    if (const auto depthLighting = ValidateDepthLightingParameters(parameters.depthLighting); !depthLighting)
    {
        return std::unexpected(depthLighting.error());
    }
    if (!IsFiniteVector(parameters.cameraPlaneCenterWorldPosition))
    {
        return std::unexpected("scene presentation camera-plane center must be finite");
    }
    if (!IsFiniteVector(parameters.cameraViewDirection))
    {
        return std::unexpected("scene presentation camera view direction must be finite and non-zero");
    }
    if (const auto direction = NormalizeViewDirection(parameters.cameraViewDirection); !direction)
    {
        return std::unexpected(direction.error());
    }
    if (!std::isfinite(parameters.fogExtinctionPerMeter) || parameters.fogExtinctionPerMeter < 0.0F)
    {
        return std::unexpected("scene presentation fog extinction must be finite and non-negative");
    }
    for (const float color : parameters.fogColorRgb)
    {
        if (!std::isfinite(color) || color < 0.0F)
        {
            return std::unexpected("scene presentation fog color must be finite and non-negative");
        }
    }
    return {};
}

std::expected<ViewPathFogSample, std::string> EvaluateViewPathFog(
    const ScenePresentationParameters& parameters,
    const std::array<float, 3>& fragmentWorldPosition)
{
    if (const auto valid = ValidateScenePresentationParameters(parameters); !valid)
    {
        return std::unexpected(valid.error());
    }
    if (!IsFiniteVector(fragmentWorldPosition))
    {
        return std::unexpected("view-path fog fragment position must be finite");
    }

    const auto normalizedDirection = NormalizeViewDirection(parameters.cameraViewDirection);
    if (!normalizedDirection)
    {
        return std::unexpected(normalizedDirection.error());
    }

    const std::array<float, 3>& planeCenter = parameters.cameraPlaneCenterWorldPosition;
    const float rayT =
        (fragmentWorldPosition[0] - planeCenter[0]) * (*normalizedDirection)[0] +
        (fragmentWorldPosition[1] - planeCenter[1]) * (*normalizedDirection)[1] +
        (fragmentWorldPosition[2] - planeCenter[2]) * (*normalizedDirection)[2];
    if (!std::isfinite(rayT))
    {
        return std::unexpected("view-path fog ray projection must be finite");
    }

    // A fragment on or behind the camera plane has no positive finite view ray and therefore no fog path.
    if (rayT <= 0.0F)
    {
        return ViewPathFogSample{};
    }
    const std::array<float, 3> rayOrigin{
        fragmentWorldPosition[0] - (*normalizedDirection)[0] * rayT,
        fragmentWorldPosition[1] - (*normalizedDirection)[1] * rayT,
        fragmentWorldPosition[2] - (*normalizedDirection)[2] * rayT};
    if (!IsFiniteVector(rayOrigin))
    {
        return std::unexpected("view-path fog ray origin must be finite");
    }

    const float surfaceLevelY = parameters.depthLighting.surfaceLevelYMeters;
    const bool rayOriginBelow = rayOrigin[1] < surfaceLevelY;
    const bool fragmentBelow = fragmentWorldPosition[1] < surfaceLevelY;
    float submergedPathLength = 0.0F;
    if (rayOriginBelow && fragmentBelow)
    {
        submergedPathLength = rayT;
    }
    else if (rayOriginBelow != fragmentBelow)
    {
        // The endpoints lie on opposite sides of the plane, so their Y delta is necessarily non-zero. The endpoint
        // exactly on the plane contributes no extra segment but keeps the below-side length intact.
        const float crossingT = std::clamp(
            (surfaceLevelY - rayOrigin[1]) / (fragmentWorldPosition[1] - rayOrigin[1]), 0.0F, 1.0F);
        submergedPathLength = rayOriginBelow ? rayT * crossingT : rayT * (1.0F - crossingT);
    }

    if (!std::isfinite(submergedPathLength) || submergedPathLength < 0.0F)
    {
        return std::unexpected("view-path fog derived path length must be finite and non-negative");
    }
    const float transmission = std::exp(-parameters.fogExtinctionPerMeter * submergedPathLength);
    if (!std::isfinite(transmission) || transmission < 0.0F || transmission > 1.0F)
    {
        return std::unexpected("view-path fog transmission must be finite and normalized");
    }
    return ViewPathFogSample{.submergedPathLengthMeters = submergedPathLength, .transmission = transmission};
}
} // namespace DeepRun::Render
