#include "Game/SurfaceFloatModel.h"

#include "Engine/Assets/AssetManager.h"

#include <array>
#include <cassert>
#include <utility>

namespace DeepRun::Game
{
Assets::ModelAsset BuildM3SurfaceFloatModel()
{
    auto id = Assets::AssetId::FromPath("game/m3_surface_float.model");
    // The literal is a permanent valid AssetId; keep the fallback unreachable without adding an error path to
    // this fixed construction helper.
    assert(id.has_value());

    Assets::ModelAsset result{.id = std::move(*id)};
    result.materials.push_back({
        .name = "M3SurfaceFloat",
        .baseColorFactor = {0.9F, 0.28F, 0.05F, 1.0F},
        .metallicFactor = 0.0F,
        .roughnessFactor = 0.65F});

    // The Jolt proxy remains the canonical 3 x 1 x 1 m box. At the fixed 600 m side-view span that box
    // would collapse into a few pixels at the waterline, so this deliberately small, high-visibility marker
    // extends above its body origin. The origin still maps directly to the Jolt body: no render offset or
    // wave-derived transform is involved.
    constexpr float HalfX = 3.0F;
    constexpr float MinimumY = -0.5F;
    constexpr float MaximumY = 3.0F;
    constexpr float HalfZ = 0.5F;
    Assets::MeshPrimitiveData primitive;
    primitive.vertices.reserve(24U);
    primitive.indices.reserve(36U);
    const auto appendFace = [&](const std::array<Assets::ModelVector3, 4>& positions,
                                const Assets::ModelVector3 normal) {
        const std::uint32_t base = static_cast<std::uint32_t>(primitive.vertices.size());
        for (const Assets::ModelVector3& position : positions)
        {
            primitive.vertices.push_back({.position = position, .normal = normal});
        }
        primitive.indices.insert(
            primitive.indices.end(), {base, base + 1U, base + 2U, base, base + 2U, base + 3U});
    };

    appendFace({{{-HalfX, MinimumY, HalfZ}, {HalfX, MinimumY, HalfZ},
                 {HalfX, MaximumY, HalfZ}, {-HalfX, MaximumY, HalfZ}}}, {0.0F, 0.0F, 1.0F});
    appendFace({{{HalfX, MinimumY, -HalfZ}, {-HalfX, MinimumY, -HalfZ},
                 {-HalfX, MaximumY, -HalfZ}, {HalfX, MaximumY, -HalfZ}}}, {0.0F, 0.0F, -1.0F});
    appendFace({{{-HalfX, MaximumY, HalfZ}, {HalfX, MaximumY, HalfZ},
                 {HalfX, MaximumY, -HalfZ}, {-HalfX, MaximumY, -HalfZ}}}, {0.0F, 1.0F, 0.0F});
    appendFace({{{-HalfX, MinimumY, -HalfZ}, {HalfX, MinimumY, -HalfZ},
                 {HalfX, MinimumY, HalfZ}, {-HalfX, MinimumY, HalfZ}}}, {0.0F, -1.0F, 0.0F});
    appendFace({{{HalfX, MinimumY, HalfZ}, {HalfX, MinimumY, -HalfZ},
                 {HalfX, MaximumY, -HalfZ}, {HalfX, MaximumY, HalfZ}}}, {1.0F, 0.0F, 0.0F});
    appendFace({{{-HalfX, MinimumY, -HalfZ}, {-HalfX, MinimumY, HalfZ},
                 {-HalfX, MaximumY, HalfZ}, {-HalfX, MaximumY, -HalfZ}}}, {-1.0F, 0.0F, 0.0F});
    primitive.materialIndex = 0U;
    primitive.localBounds = {.minimum = {-HalfX, MinimumY, -HalfZ}, .maximum = {HalfX, MaximumY, HalfZ}};
    primitive.hasNormals = true;
    result.primitives.push_back(std::move(primitive));
    result.nodes.push_back({.name = "M3SurfaceFloat", .localToModel = {}, .primitiveIndices = {0U}});
    result.bounds = {.minimum = {-HalfX, MinimumY, -HalfZ}, .maximum = {HalfX, MaximumY, HalfZ}};
    return result;
}
}
