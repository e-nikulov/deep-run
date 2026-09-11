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
// M5-H.4 presentation policy. The accepted M3 environment remains the only authoritative local section;
// these values only decide how that already-uploaded visual representation is repeated while the player
// views a wider tactical frame. No additional physics, navigation, acoustic terrain or gameplay state exists.
inline constexpr float M5DetailedEnvironmentMaximumHorizontalSpanMeters = 9'000.0F;
inline constexpr std::size_t M5DetailedEnvironmentMaximumVisibleTileCount = 16U;

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
    // A small deterministic luminance variation breaks the obvious copy/paste cadence without inventing a
    // second terrain representation. Tile zero stays exactly authored; neighbouring copies vary by <= 7%.
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
