#pragma once

#include "Engine/Assets/ModelAsset.h"
#include "Engine/Physics/PhysicsTypes.h"
#include "Engine/Render/Camera.h"
#include "Engine/Render/ClearRect.h"

#include <expected>
#include <optional>
#include <string>

namespace DeepRun::Game
{
// M2 Slice D2 presentation tuning (Game-owned). These are temporary visual values for the flat-water
// cross-section, not an art direction and not an architectural contract: M3 may replace the whole
// presentation path without touching the authoritative WaterBody. The above-water region is the engine's
// existing background clear; only the underwater fill color is applied by the Game through the generic
// renderer clear-rect API.
constexpr Render::RgbaColor M2AboveWaterBackgroundColor{0.015F, 0.055F, 0.075F, 1.0F};
constexpr Render::RgbaColor M2UnderwaterBackgroundColor{0.04F, 0.22F, 0.38F, 1.0F};

// World placement of a body whose model-space bounds center must sit at the given depth below the water
// surface (M2 Slice D2). The asset Y center is deliberately NOT used as a depth: only X/Z come from the
// asset-space bounds center; world Y derives exclusively from the authoritative surface level and the
// desired depth. Pure function, no WaterBody dependency, so placement can be tested against arbitrary
// surfaces (the integration helper must not assume sea level at world Y=0).
[[nodiscard]] Physics::PhysicsVector3 ComputeInitialBodyWorldCenter(
    const float surfaceLevelY,
    const float desiredDepthMeters,
    const Assets::ModelVector3& assetBoundsCenter) noexcept;

// Projects the authoritative flat water surface (world-space Y) through the actual gameplay camera into a
// normalized viewport coordinate: 0 = top edge, 1 = bottom edge of the current render target. The world
// point used is (camera.target.x, surfaceLevelY, camera.target.z); for the M2 orthographic side view the
// result depends only on world Y and the camera span/target, never on a hard-coded pixel position. W is
// divided out explicitly even though the current camera is orthographic (W == 1). Non-finite input or a
// non-finite projection result is rejected as a recoverable error — no NaN ever reaches the renderer.
[[nodiscard]] std::expected<float, std::string> ProjectWorldSurfaceToViewportY(
    const Render::OrthographicCamera& camera,
    float surfaceLevelY);

// The underwater region of the viewport for a projected waterline (M2 Slice D2 cross-section presentation):
//   - waterline above the viewport top  -> the entire viewport is underwater ({0, 0, 1, 1})
//   - waterline inside the viewport     -> the region from the waterline to the bottom edge
//   - waterline at/below the bottom     -> no visible underwater region (nullopt)
// The result is a generic renderer-neutral ViewportRect; all camera clipping happens here, in the Game
// projection helper, BEFORE the renderer is called. Non-finite input is rejected as an error instead of
// producing an invalid rectangle. This never modifies authoritative water state: it only derives what to
// paint from the already-sampled surface level and the current camera.
[[nodiscard]] std::expected<std::optional<Render::ViewportRect>, std::string> UnderwaterRegionForSurface(
    const Render::OrthographicCamera& camera,
    float surfaceLevelY);
}
