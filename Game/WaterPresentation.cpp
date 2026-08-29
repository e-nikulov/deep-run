#include "Game/WaterPresentation.h"

#include <algorithm>
#include <cmath>

namespace DeepRun::Game
{
Physics::PhysicsVector3 ComputeInitialBodyWorldCenter(
    const float surfaceLevelY,
    const float desiredDepthMeters,
    const Assets::ModelVector3& assetBoundsCenter) noexcept
{
    return {
        .x = assetBoundsCenter.x,
        // Depth is measured from the authoritative surface level, never from world Y=0 and never from the
        // asset-space Y center: signedDepthMeters = surfaceLevelY - bodyWorldY.
        .y = surfaceLevelY - desiredDepthMeters,
        .z = assetBoundsCenter.z};
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

    // The surface point sits in the gameplay plane at the camera target's X/Z; for a flat horizontal
    // surface only its world Y matters, but projecting the full point keeps this helper honest about using
    // the actual camera view-projection instead of a duplicated manual formula.
    const std::array<float, 4> clip = Render::TransformPoint(
        camera.viewProjection,
        {camera.target.x, surfaceLevelY, camera.target.z});
    if (!std::isfinite(clip[0]) || !std::isfinite(clip[1]) || !std::isfinite(clip[2]) ||
        !std::isfinite(clip[3]))
    {
        return std::unexpected("water surface projection produced non-finite clip coordinates");
    }

    // Divide by W explicitly even though the current camera is orthographic (W == 1): the contract must not
    // silently break if a perspective camera ever reaches this helper.
    const float w = clip[3];
    if (!std::isfinite(w) || std::abs(w) < 1.0e-8F)
    {
        return std::unexpected("water surface projection has a degenerate W");
    }

    // NDC Y (-1 bottom .. +1 top) -> normalized viewport Y from the top (0 top .. 1 bottom).
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
    // Sub-pixel slivers are not visible regions: a waterline within this normalized distance of the bottom
    // edge paints nothing (at 720p this is well under one pixel). This keeps the helper from ever emitting
    // an invalid/empty rectangle for borderline projections.
    constexpr float SurfaceEdgeEpsilon = 1.0e-4F;

    const auto surfaceNdcYFromTop = ProjectWorldSurfaceToViewportY(camera, surfaceLevelY);
    if (!surfaceNdcYFromTop)
    {
        return std::unexpected(surfaceNdcYFromTop.error());
    }

    // Surface at or below the viewport bottom: no visible underwater region.
    if (*surfaceNdcYFromTop >= 1.0F - SurfaceEdgeEpsilon)
    {
        return std::optional<Render::ViewportRect>{};
    }

    // Surface above the top edge -> whole viewport is underwater; inside -> band from the waterline down to
    // the bottom edge. Clamping absorbs floating-point drift at the edges; the renderer validates again.
    const float top = std::clamp(*surfaceNdcYFromTop, 0.0F, 1.0F);
    return std::optional<Render::ViewportRect>(Render::ViewportRect{
        .left = 0.0F,
        .top = top,
        .right = 1.0F,
        .bottom = 1.0F});
}
}
