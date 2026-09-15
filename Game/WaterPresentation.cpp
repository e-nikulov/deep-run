#include "Game/WaterPresentation.h"

#include <algorithm>
#include <cmath>

namespace DeepRun::Game
{
static_assert(Render::GerstnerWaveComponentCapacity == Marine::WaterWaveComponentCapacity);

Render::GerstnerSurfacePresentationParameters BuildGerstnerSurfacePresentation(const Marine::WaterBody& water)
{
    Render::GerstnerSurfacePresentationParameters result{
        .minimumX = -340.0F, .maximumX = 340.0F,
        .referenceLevelY = water.Config().surfaceLevelY, .bottomFillY = -600.0F,
        .horizontalSampleCount = 257U,
        .activeComponentCount = 0U,
        .deepFillRgb = {M2UnderwaterBackgroundColor.r, M2UnderwaterBackgroundColor.g, M2UnderwaterBackgroundColor.b},
        .surfaceTintRgb = {0.0065F, 0.075F, 0.18F}};
    if (water.Config().waves)
    {
        result.activeComponentCount = water.Config().waves->activeComponentCount;
        for (std::size_t i = 0; i < result.activeComponentCount; ++i)
        {
            const auto& wave = water.Config().waves->components[i];
            result.components[i] = {wave.amplitudeMeters, wave.wavelengthMeters,
                wave.angularFrequencyRadiansPerSecond, wave.phaseOffsetRadians, wave.horizontalSteepness};
        }
    }
    return result;
}

Physics::PhysicsVector3 ComputeInitialBodyWorldCenter(
    const float surfaceLevelY,
    const float desiredDepthMeters,
    const Assets::ModelVector3& referencePoint) noexcept
{
    return {
        .x = referencePoint.x,
        .y = surfaceLevelY - desiredDepthMeters,
        .z = referencePoint.z};
}

std::expected<float, std::string> ProjectWorldSurfaceToViewportY(
    const Render::OrthographicCamera& camera,
    const float surfaceLevelY)
{
    if (!std::isfinite(surfaceLevelY))
    {
        return std::unexpected("water surface level is not finite");
    }
    if (!Render::IsFinite(camera.viewProjection) || !std::isfinite(camera.width) ||
        !std::isfinite(camera.height) || camera.width <= 0.0F || camera.height <= 0.0F)
    {
        return std::unexpected("camera projection data is not finite");
    }

    const std::array<float, 4> clip = Render::TransformPoint(
        camera.viewProjection,
        {camera.target.x, surfaceLevelY, camera.target.z});
    if (!std::isfinite(clip[0]) || !std::isfinite(clip[1]) || !std::isfinite(clip[2]) ||
        !std::isfinite(clip[3]))
    {
        return std::unexpected("water surface projection produced non-finite clip coordinates");
    }

    const float w = clip[3];
    if (!std::isfinite(w) || std::abs(w) < 1.0e-8F)
    {
        return std::unexpected("water surface projection has a degenerate W");
    }

    const float ndcY = clip[1] / w;
    if (!std::isfinite(ndcY))
    {
        return std::unexpected("water surface projection produced a non-finite NDC coordinate");
    }
    const float viewportYFromTop = (1.0F - ndcY) * 0.5F;
    if (!std::isfinite(viewportYFromTop))
    {
        return std::unexpected("water surface projection produced a non-finite viewport coordinate");
    }
    return viewportYFromTop;
}

std::expected<std::optional<Render::ViewportRect>, std::string> UnderwaterRegionForSurface(
    const Render::OrthographicCamera& camera,
    const float surfaceLevelY)
{
    constexpr float SurfaceEdgeEpsilon = 1.0e-4F;

    const auto surfaceNdcYFromTop = ProjectWorldSurfaceToViewportY(camera, surfaceLevelY);
    if (!surfaceNdcYFromTop)
    {
        return std::unexpected(surfaceNdcYFromTop.error());
    }

    if (*surfaceNdcYFromTop >= 1.0F - SurfaceEdgeEpsilon)
    {
        return std::optional<Render::ViewportRect>{};
    }

    const float top = std::clamp(*surfaceNdcYFromTop, 0.0F, 1.0F);
    return std::optional<Render::ViewportRect>(Render::ViewportRect{
        .left = 0.0F,
        .top = top,
        .right = 1.0F,
        .bottom = 1.0F});
}
}
