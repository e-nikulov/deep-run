#pragma once

#include "Engine/Render/ModelDraw.h"

#include <cmath>
#include <cstddef>
#include <expected>
#include <limits>
#include <span>
#include <string>
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

[[nodiscard]] inline std::vector<Render::ModelDrawInstance> BuildEnvironmentPresentationDraws(
    const std::span<const Render::ModelDrawInstance> baseDraws,
    const std::span<const EnvironmentPresentationTile> tiles)
{
    std::vector<Render::ModelDrawInstance> result;
    result.reserve(baseDraws.size() * tiles.size());
    for (const EnvironmentPresentationTile& tile : tiles)
    {
        Assets::ModelTransform tileTransform{};
        tileTransform.values[12] = tile.offsetXMeters;
        tileTransform.values[13] = tile.offsetYMeters;
        for (const Render::ModelDrawInstance& baseDraw : baseDraws)
        {
            Render::ModelDrawInstance draw = baseDraw;
            draw.modelToWorld = Render::Multiply(tileTransform, baseDraw.modelToWorld);
            // Translation does not alter normals; retaining the prepared normal transform avoids rebuilding
            // presentation state per tile and cannot affect geometry/physics authority.
            result.push_back(std::move(draw));
        }
    }
    return result;
}
} // namespace DeepRun::Game
