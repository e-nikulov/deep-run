#pragma once

#include "Game/Combat/CombatPlaygroundCamera.h"
#include "Game/Environment/ScalableEnvironmentPresentation.h"
#include "Game/Environment/VerticalOceanGameplayContract.h"

#include <cmath>
#include <span>
#include <vector>

namespace DeepRun::Tests
{
[[nodiscard]] inline bool RunM5ScalableEnvironmentPresentationChecks()
{
    using namespace Game;

    if (NormalGameplaySeaSurfaceReferenceYMeters != 0.0F ||
        NormalGameplayMaximumVisibleDepthMeters != 700.0F ||
        NormalGameplayOceanBottomReferenceYMeters != -700.0F ||
        NormalGameplayPeriscopeZoneMaximumDepthMeters != 20.0F ||
        NormalGameplayShallowZoneMaximumDepthMeters != 100.0F ||
        NormalGameplayPrimaryCombatZoneMaximumDepthMeters != 300.0F ||
        NormalGameplayDeepTacticalZoneMaximumDepthMeters != 500.0F ||
        NormalGameplayExtremeZoneMaximumDepthMeters != 600.0F ||
        NormalGameplayLowerBoundaryMaximumDepthMeters != 700.0F ||
        !IsWithinNormalGameplayDepthMeters(0.0F) ||
        !IsWithinNormalGameplayDepthMeters(700.0F) ||
        IsWithinNormalGameplayDepthMeters(700.001F) ||
        !IsWithinNormalGameplayReferenceYMeters(0.0F) ||
        !IsWithinNormalGameplayReferenceYMeters(-700.0F) ||
        IsWithinNormalGameplayReferenceYMeters(-700.001F) ||
        !IsWithinNormalGameplayWaterColumn(-650.0F, 0.0F) ||
        IsWithinNormalGameplayWaterColumn(-701.0F, 0.0F))
    {
        return false;
    }

    for (const auto& point : M5StrategicSeabedProfile)
    {
        if (!IsWithinNormalGameplayReferenceYMeters(point.yMeters))
        {
            return false;
        }
    }
    // The strategic render skirt is deliberately outside the D0 gameplay ocean. It exists only to prevent a
    // visible underside and must not be mistaken for playable/authored seabed depth.
    if (M5StrategicSeabedExtrusionBottomYMeters >= NormalGameplayOceanBottomReferenceYMeters ||
        IsWithinNormalGameplayReferenceYMeters(M5StrategicSeabedExtrusionBottomYMeters))
    {
        return false;
    }

    if (!UseDetailedEnvironmentPresentation(600.0F) ||
        !UseDetailedEnvironmentPresentation(M5DetailedEnvironmentMaximumHorizontalSpanMeters) ||
        UseDetailedEnvironmentPresentation(M5DetailedEnvironmentMaximumHorizontalSpanMeters + 1.0F) ||
        UseDetailedEnvironmentPresentation(0.0F))
    {
        return false;
    }
    if (!UseStrategicSeabedPresentation(600.0F) ||
        !UseStrategicSeabedPresentation(Combat::M5CombatLocalCameraHorizontalSpanMeters) ||
        !UseStrategicSeabedPresentation(3'600.0F) ||
        !UseStrategicSeabedPresentation(M5StrategicSeabedMaximumHorizontalSpanMeters) ||
        UseStrategicSeabedPresentation(0.0F) ||
        UseStrategicSeabedPresentation(M5StrategicSeabedMaximumHorizontalSpanMeters + 1.0F))
    {
        return false;
    }

    const auto strategic = BuildStrategicSeabedPresentationModel();
    if (!strategic || strategic->primitives.size() != 1U ||
        strategic->primitives[0].vertices.size() != 192U ||
        strategic->primitives[0].indices.size() != 288U ||
        std::abs(strategic->bounds.minimum.y - M5StrategicSeabedExtrusionBottomYMeters) > 0.001F)
    {
        return false;
    }

    if (!HorizontalPresentationBoundsCoverView(-400.0F, 400.0F, 0.0F, 600.0F) ||
        !HorizontalPresentationBoundsCoverView(-400.0F, 400.0F, 0.0F, 800.0F) ||
        HorizontalPresentationBoundsCoverView(-400.0F, 400.0F, 120.0F, 600.0F) ||
        HorizontalPresentationBoundsCoverView(-400.0F, 400.0F, 0.0F, 801.0F))
    {
        return false;
    }

    const auto localTiles = BuildEnvironmentPresentationTiles(-400.0F, 400.0F, 0.0F, 600.0F, -5.0F);
    const auto exactLocalTiles = BuildEnvironmentPresentationTiles(-400.0F, 400.0F, 0.0F, 800.0F, -5.0F);
    const auto pannedTiles = BuildEnvironmentPresentationTiles(-400.0F, 400.0F, 120.0F, 600.0F, -5.0F);
    const auto widerTiles = BuildEnvironmentPresentationTiles(-400.0F, 400.0F, 0.0F, 801.0F, -5.0F);
    if (!localTiles || !exactLocalTiles || !pannedTiles || !widerTiles ||
        localTiles->size() != 1U || exactLocalTiles->size() != 1U ||
        !pannedTiles->empty() || !widerTiles->empty() ||
        localTiles->front().index != 0 || localTiles->front().offsetXMeters != 0.0F ||
        localTiles->front().offsetYMeters != 0.0F)
    {
        return false;
    }

    Render::ModelDrawInstance source{};
    source.modelToWorld.translation = {10.0F, 20.0F, 30.0F};
    source.materialBaseColorMultiplier = 0.5F;
    const std::vector<Render::ModelDrawInstance> localDraws = BuildEnvironmentPresentationDraws(
        std::span<const Render::ModelDrawInstance>(&source, 1U),
        std::span<const EnvironmentPresentationTile>(*localTiles));
    if (localDraws.size() != 1U ||
        std::abs(localDraws[0].modelToWorld.translation.x - 10.0F) > 0.001F ||
        std::abs(localDraws[0].modelToWorld.translation.y - 20.0F) > 0.001F ||
        std::abs(localDraws[0].materialBaseColorMultiplier - 0.5F) > 0.001F)
    {
        return false;
    }

    source.materialName = std::string(ScalableEnvironmentPresentationDetail::DefaultM5SuppressedTiledMaterial);
    const auto genericIce = BuildEnvironmentPresentationDraws(
        std::span<const Render::ModelDrawInstance>(&source, 1U),
        std::span<const EnvironmentPresentationTile>(*localTiles));
    const auto scenarioIce = BuildEnvironmentPresentationDraws(
        std::span<const Render::ModelDrawInstance>(&source, 1U),
        std::span<const EnvironmentPresentationTile>(*localTiles),
        ScalableEnvironmentPresentationDetail::DefaultM5SuppressedTiledMaterial);
    return genericIce.empty() && scenarioIce.size() == 1U;
}
} // namespace DeepRun::Tests
