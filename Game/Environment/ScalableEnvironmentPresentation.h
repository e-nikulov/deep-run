#pragma once

#include "Engine/Render/ModelDraw.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <expected>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace DeepRun::Game
{
// M5-H.4/M5-V1 presentation policy. The accepted M3 environment remains the only authoritative local section.
// Reusing that 800 m section is acceptable only while the view is still genuinely local. Beyond 2 km the
// copied relief becomes more visually misleading than useful, so tactical-wide presentation uses one fixed,
// coarse strategic silhouette instead of wallpapering identical local geometry.
// No additional physics, navigation, acoustic terrain or gameplay state exists.
inline constexpr float M5DetailedEnvironmentMaximumHorizontalSpanMeters = 2'000.0F;
inline constexpr std::size_t M5DetailedEnvironmentMaximumVisibleTileCount = 16U;
inline constexpr float M5StrategicSeabedMinimumHorizontalSpanMeters =
    M5DetailedEnvironmentMaximumHorizontalSpanMeters;
inline constexpr float M5StrategicSeabedMaximumHorizontalSpanMeters = 12'000.0F;
inline constexpr float M5StrategicSeabedFillBottomYMeters = -650.0F;
inline constexpr float M5StrategicSeabedFrontZMeters = 3.0F;
inline constexpr float M5StrategicSeabedBackZMeters = -3.0F;

struct StrategicSeabedPresentationPoint final
{
    float xMeters = 0.0F;
    float yMeters = 0.0F;
};

// One finite, authored low-frequency silhouette for M5 tactical-wide presentation. It is deliberately not
// derived from the local M3 profile, and it has no physics/navigation/acoustic consumer. Its fixed knots avoid
// both obvious local-tile repetition and a general procedural-world system.
inline constexpr std::array<StrategicSeabedPresentationPoint, 17> M5StrategicSeabedProfile{{
    {-12'000.0F, -286.0F}, {-10'500.0F, -238.0F}, {-9'000.0F, -322.0F}, {-7'500.0F, -264.0F},
    {-6'000.0F, -374.0F}, {-4'500.0F, -302.0F}, {-3'000.0F, -346.0F}, {-1'500.0F, -248.0F},
    {0.0F, -330.0F}, {1'500.0F, -278.0F}, {3'000.0F, -402.0F}, {4'500.0F, -316.0F},
    {6'000.0F, -232.0F}, {7'500.0F, -358.0F}, {9'000.0F, -294.0F}, {10'500.0F, -438.0F},
    {12'000.0F, -340.0F}}};

struct EnvironmentPresentationTile final
{
    int index = 0;
    float offsetXMeters = 0.0F;
    float offsetYMeters = 0.0F;
};

[[nodiscard]] inline bool HorizontalPresentationBoundsCoverView(
    const float authoredMinimumX,
    const float authoredMaximumX,
    const float cameraTargetX,
    const float cameraHorizontalSpanMeters) noexcept
{
    if (!std::isfinite(authoredMinimumX) || !std::isfinite(authoredMaximumX) ||
        !std::isfinite(cameraTargetX) || !std::isfinite(cameraHorizontalSpanMeters) ||
        authoredMaximumX <= authoredMinimumX || cameraHorizontalSpanMeters <= 0.0F)
    {
        return false;
    }
    const float halfSpan = 0.5F * cameraHorizontalSpanMeters;
    return cameraTargetX - halfSpan >= authoredMinimumX &&
           cameraTargetX + halfSpan <= authoredMaximumX;
}

[[nodiscard]] inline bool UseDetailedEnvironmentPresentation(const float cameraHorizontalSpanMeters) noexcept
{
    return std::isfinite(cameraHorizontalSpanMeters) && cameraHorizontalSpanMeters > 0.0F &&
           cameraHorizontalSpanMeters <= M5DetailedEnvironmentMaximumHorizontalSpanMeters;
}

[[nodiscard]] inline bool UseStrategicSeabedPresentation(const float cameraHorizontalSpanMeters) noexcept
{
    return std::isfinite(cameraHorizontalSpanMeters) &&
           cameraHorizontalSpanMeters > M5StrategicSeabedMinimumHorizontalSpanMeters &&
           cameraHorizontalSpanMeters <= M5StrategicSeabedMaximumHorizontalSpanMeters;
}

[[nodiscard]] inline std::expected<Assets::ModelAsset, std::string>
BuildStrategicSeabedPresentationModel()
{
    const auto assetId = Assets::AssetId::FromPath("environment/m5-strategic-seabed.presentation.model");
    if (!assetId)
    {
        return std::unexpected("strategic seabed presentation could not create a stable asset id: " + assetId.error());
    }

    Assets::ModelAsset asset{.id = *assetId};
    asset.materials.push_back(Assets::ModelMaterialData{
        .name = "M5StrategicSeabed",
        .baseColorFactor = {0.085F, 0.115F, 0.105F, 1.0F},
        .metallicFactor = 0.0F,
        .roughnessFactor = 0.96F});

    Assets::MeshPrimitiveData primitive;
    primitive.materialIndex = 0U;
    primitive.hasNormals = true;
    primitive.vertices.reserve((M5StrategicSeabedProfile.size() - 1U) * 16U);
    primitive.indices.reserve((M5StrategicSeabedProfile.size() - 1U) * 24U);

    Assets::ModelVector3 boundsMinimum{
        (std::numeric_limits<float>::max)(),
        (std::numeric_limits<float>::max)(),
        (std::numeric_limits<float>::max)()};
    Assets::ModelVector3 boundsMaximum{
        (std::numeric_limits<float>::lowest)(),
        (std::numeric_limits<float>::lowest)(),
        (std::numeric_limits<float>::lowest)()};
    const auto extendBounds = [&boundsMinimum, &boundsMaximum](const Assets::ModelVector3 position) {
        boundsMinimum.x = (std::min)(boundsMinimum.x, position.x);
        boundsMinimum.y = (std::min)(boundsMinimum.y, position.y);
        boundsMinimum.z = (std::min)(boundsMinimum.z, position.z);
        boundsMaximum.x = (std::max)(boundsMaximum.x, position.x);
        boundsMaximum.y = (std::max)(boundsMaximum.y, position.y);
        boundsMaximum.z = (std::max)(boundsMaximum.z, position.z);
    };
    const auto pushVertex = [&primitive, &extendBounds](
        const float x, const float y, const float z, const Assets::ModelVector3 normal) {
        const Assets::ModelVector3 position{x, y, z};
        primitive.vertices.push_back(Assets::MeshVertex{.position = position, .normal = normal});
        extendBounds(position);
    };
    const auto pushTriangle = [&primitive](const std::uint32_t base, const std::uint32_t a,
                                            const std::uint32_t b, const std::uint32_t c) {
        primitive.indices.insert(primitive.indices.end(), {base + a, base + b, base + c});
    };

    for (std::size_t index = 0U; index + 1U < M5StrategicSeabedProfile.size(); ++index)
    {
        const auto first = M5StrategicSeabedProfile[index];
        const auto second = M5StrategicSeabedProfile[index + 1U];
        const float dx = second.xMeters - first.xMeters;
        const float dy = second.yMeters - first.yMeters;
        const float normalLength = std::hypot(dx, dy);
        if (!std::isfinite(dx) || !std::isfinite(dy) || !(dx > 0.0F) || !std::isfinite(normalLength) ||
            !(normalLength > 0.0F) || first.yMeters >= 0.0F || second.yMeters >= 0.0F)
        {
            return std::unexpected("strategic seabed presentation profile is invalid");
        }
        const Assets::ModelVector3 topNormal{-dy / normalLength, dx / normalLength, 0.0F};
        const std::uint32_t base = static_cast<std::uint32_t>(primitive.vertices.size());
        pushVertex(first.xMeters, first.yMeters, M5StrategicSeabedBackZMeters, topNormal);
        pushVertex(second.xMeters, second.yMeters, M5StrategicSeabedBackZMeters, topNormal);
        pushVertex(second.xMeters, second.yMeters, M5StrategicSeabedFrontZMeters, topNormal);
        pushVertex(first.xMeters, first.yMeters, M5StrategicSeabedFrontZMeters, topNormal);
        pushVertex(first.xMeters, first.yMeters, M5StrategicSeabedFrontZMeters, {0.0F, 0.0F, 1.0F});
        pushVertex(second.xMeters, second.yMeters, M5StrategicSeabedFrontZMeters, {0.0F, 0.0F, 1.0F});
        pushVertex(second.xMeters, M5StrategicSeabedFillBottomYMeters, M5StrategicSeabedFrontZMeters,
                   {0.0F, 0.0F, 1.0F});
        pushVertex(first.xMeters, M5StrategicSeabedFillBottomYMeters, M5StrategicSeabedFrontZMeters,
                   {0.0F, 0.0F, 1.0F});
        pushVertex(first.xMeters, first.yMeters, M5StrategicSeabedBackZMeters, {0.0F, 0.0F, -1.0F});
        pushVertex(second.xMeters, second.yMeters, M5StrategicSeabedBackZMeters, {0.0F, 0.0F, -1.0F});
        pushVertex(second.xMeters, M5StrategicSeabedFillBottomYMeters, M5StrategicSeabedBackZMeters,
                   {0.0F, 0.0F, -1.0F});
        pushVertex(first.xMeters, M5StrategicSeabedFillBottomYMeters, M5StrategicSeabedBackZMeters,
                   {0.0F, 0.0F, -1.0F});
        pushVertex(first.xMeters, M5StrategicSeabedFillBottomYMeters, M5StrategicSeabedBackZMeters,
                   {0.0F, -1.0F, 0.0F});
        pushVertex(second.xMeters, M5StrategicSeabedFillBottomYMeters, M5StrategicSeabedBackZMeters,
                   {0.0F, -1.0F, 0.0F});
        pushVertex(second.xMeters, M5StrategicSeabedFillBottomYMeters, M5StrategicSeabedFrontZMeters,
                   {0.0F, -1.0F, 0.0F});
        pushVertex(first.xMeters, M5StrategicSeabedFillBottomYMeters, M5StrategicSeabedFrontZMeters,
                   {0.0F, -1.0F, 0.0F});

        pushTriangle(base, 0U, 2U, 1U);
        pushTriangle(base, 0U, 3U, 2U);
        pushTriangle(base, 4U, 6U, 5U);
        pushTriangle(base, 4U, 7U, 6U);
        pushTriangle(base, 8U, 9U, 10U);
        pushTriangle(base, 8U, 10U, 11U);
        pushTriangle(base, 12U, 13U, 14U);
        pushTriangle(base, 12U, 14U, 15U);
    }

    primitive.localBounds = Assets::ModelBounds{boundsMinimum, boundsMaximum};
    asset.primitives.push_back(std::move(primitive));
    asset.nodes.push_back(Assets::MeshNodeData{
        .name = "M5StrategicSeabed",
        .localToModel = {},
        .primitiveIndices = {0U}});
    asset.bounds = Assets::ModelBounds{boundsMinimum, boundsMaximum};
    return asset;
}

[[nodiscard]] inline std::expected<std::vector<EnvironmentPresentationTile>, std::string>
BuildEnvironmentPresentationTiles(
    const float authoredMinimumX,
    const float authoredMaximumX,
    const float cameraTargetX,
    const float cameraHorizontalSpanMeters,
    const float verticalStepPerTileMeters = 0.0F)
{
    if (!std::isfinite(authoredMinimumX) || !std::isfinite(authoredMaximumX) ||
        !std::isfinite(cameraTargetX) || !std::isfinite(cameraHorizontalSpanMeters) ||
        !std::isfinite(verticalStepPerTileMeters) || authoredMaximumX <= authoredMinimumX ||
        cameraHorizontalSpanMeters <= 0.0F)
    {
        return std::unexpected("scalable environment presentation received invalid bounds or camera framing");
    }
    if (!UseDetailedEnvironmentPresentation(cameraHorizontalSpanMeters))
    {
        return std::vector<EnvironmentPresentationTile>{};
    }

    const double tileWidth = static_cast<double>(authoredMaximumX) - authoredMinimumX;
    const double halfSpan = 0.5 * static_cast<double>(cameraHorizontalSpanMeters);
    const double viewMinimumX = static_cast<double>(cameraTargetX) - halfSpan;
    const double viewMaximumX = static_cast<double>(cameraTargetX) + halfSpan;
    const double firstTileValue = std::ceil((viewMinimumX - authoredMaximumX) / tileWidth);
    const double lastTileValue = std::floor((viewMaximumX - authoredMinimumX) / tileWidth);
    if (!std::isfinite(firstTileValue) || !std::isfinite(lastTileValue) ||
        firstTileValue < static_cast<double>((std::numeric_limits<int>::min)()) ||
        lastTileValue > static_cast<double>((std::numeric_limits<int>::max)()))
    {
        return std::unexpected("scalable environment presentation tile range is not representable");
    }

    const int firstTile = static_cast<int>(firstTileValue);
    const int lastTile = static_cast<int>(lastTileValue);
    if (lastTile < firstTile)
    {
        return std::vector<EnvironmentPresentationTile>{};
    }
    const std::size_t tileCount = static_cast<std::size_t>(lastTile - firstTile + 1);
    if (tileCount > M5DetailedEnvironmentMaximumVisibleTileCount)
    {
        return std::unexpected("scalable environment presentation exceeded its bounded tactical tile budget");
    }

    std::vector<EnvironmentPresentationTile> tiles;
    tiles.reserve(tileCount);
    for (int tileIndex = firstTile; tileIndex <= lastTile; ++tileIndex)
    {
        tiles.push_back(EnvironmentPresentationTile{
            .index = tileIndex,
            .offsetXMeters = static_cast<float>(static_cast<double>(tileIndex) * tileWidth),
            .offsetYMeters = static_cast<float>(
                static_cast<double>(tileIndex) * static_cast<double>(verticalStepPerTileMeters))});
    }
    return tiles;
}

namespace ScalableEnvironmentPresentationDetail
{
// Generic M5 combat must not inherit the three large M3 ice formations as a repeated Arctic signature.
// This is presentation gating only: the accepted M3 local scene and its coarse collision bodies are untouched.
// A future explicitly authored ice scenario can opt back in at this helper boundary without adding new physics.
inline constexpr std::string_view DefaultM5SuppressedTiledMaterial = "UnderwaterIce";

[[nodiscard]] inline float TileMaterialVariation(const int tileIndex) noexcept
{
    // A small deterministic luminance variation breaks the obvious copy/paste cadence while multiple local
    // tiles are still justified. Tile zero stays exactly authored; neighbouring copies vary by <= 7%.
    constexpr std::array<float, 7> factors{1.0F, 0.94F, 1.05F, 0.97F, 1.07F, 0.95F, 1.03F};
    const long long signedIndex = static_cast<long long>(tileIndex);
    const std::size_t index = static_cast<std::size_t>(signedIndex < 0 ? -signedIndex : signedIndex) % factors.size();
    return factors[index];
}
}

[[nodiscard]] inline std::vector<Render::ModelDrawInstance> BuildEnvironmentPresentationDraws(
    const std::span<const Render::ModelDrawInstance> baseDraws,
    const std::span<const EnvironmentPresentationTile> tiles,
    const bool includeScenarioIce = false)
{
    std::vector<Render::ModelDrawInstance> result;
    result.reserve(baseDraws.size() * tiles.size());
    for (const EnvironmentPresentationTile& tile : tiles)
    {
        Assets::ModelTransform tileTransform{};
        tileTransform.values[12] = tile.offsetXMeters;
        tileTransform.values[13] = tile.offsetYMeters;
        const float materialVariation = ScalableEnvironmentPresentationDetail::TileMaterialVariation(tile.index);
        for (const Render::ModelDrawInstance& baseDraw : baseDraws)
        {
            if (!includeScenarioIce &&
                baseDraw.material.name == ScalableEnvironmentPresentationDetail::DefaultM5SuppressedTiledMaterial)
            {
                continue;
            }

            Render::ModelDrawInstance draw = baseDraw;
            draw.modelToWorld = Render::Multiply(tileTransform, baseDraw.modelToWorld);
            // Translation does not alter normals; retaining the prepared normal transform avoids rebuilding
            // presentation state per tile and cannot affect geometry/physics authority.
            draw.material.baseColorFactor[0] = std::clamp(
                draw.material.baseColorFactor[0] * materialVariation, 0.0F, 1.0F);
            draw.material.baseColorFactor[1] = std::clamp(
                draw.material.baseColorFactor[1] * materialVariation, 0.0F, 1.0F);
            draw.material.baseColorFactor[2] = std::clamp(
                draw.material.baseColorFactor[2] * materialVariation, 0.0F, 1.0F);
            result.push_back(std::move(draw));
        }
    }
    return result;
}
} // namespace DeepRun::Game
