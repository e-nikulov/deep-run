#pragma once

#include "Game/Combat/CombatPlaygroundCamera.h"
#include "Game/Environment/ScalableEnvironmentPresentation.h"
#include "Game/Environment/VerticalOceanGameplayContract.h"

#include <cmath>
#include <limits>
#include <span>
#include <vector>

namespace DeepRun::Tests
{
[[nodiscard]] inline bool RunM5ScalableEnvironmentPresentationChecks()
{
    using namespace Game;

    if (NormalGameplaySeaSurfaceReferenceYMeters != 0.0F ||
        NormalGameplayMaximumVisibleDepthMeters != 700.0F ||
        NormalGameplayBandBottomReferenceYMeters != -700.0F ||
        NormalGameplayOceanBottomReferenceYMeters != NormalGameplayBandBottomReferenceYMeters ||
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

    // The normal gameplay band is not a seabed/world-end declaration.
    if (ClassifyBathymetryDepthMeters(250.0F) != BathymetryDepthBand::Shelf ||
        ClassifyBathymetryDepthMeters(650.0F) != BathymetryDepthBand::Continental ||
        ClassifyBathymetryDepthMeters(1'500.0F) != BathymetryDepthBand::DeepOcean ||
        ClassifyBathymetryDepthMeters(2'500.0F) != BathymetryDepthBand::Abyssal)
    {
        return false;
    }

    const float localSky = AboveWaterFractionForPresentationSpanMeters(800.0F);
    const float transitionSky = AboveWaterFractionForPresentationSpanMeters(1'550.0F);
    const float tacticalSky = AboveWaterFractionForPresentationSpanMeters(3'600.0F);
    const float operationalSky = AboveWaterFractionForPresentationSpanMeters(10'000.0F);
    const float strategicSky = AboveWaterFractionForPresentationSpanMeters(200'000.0F);
    if (std::abs(localSky - 0.15F) > 0.0001F ||
        !(transitionSky > localSky && transitionSky < tacticalSky) ||
        std::abs(tacticalSky - 0.32F) > 0.0001F ||
        std::abs(operationalSky - 0.36F) > 0.0001F ||
        std::abs(strategicSky - 0.40F) > 0.0001F)
    {
        return false;
    }

    // The legacy render skirt is not gameplay seabed. It may extend below the normal band because it is only
    // an implementation detail for an explicitly-known temporary strategic profile.
    if (M5StrategicSeabedExtrusionBottomYMeters >= NormalGameplayBandBottomReferenceYMeters ||
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

    // Generic M5 wide view must NOT invent bathymetry. The temporary strategic profile is explicit opt-in only.
    if (UseStrategicSeabedPresentation(3'600.0F) ||
        UseStrategicSeabedPresentation(600.0F, true) ||
        !UseStrategicSeabedPresentation(3'600.0F, true) ||
        !UseStrategicSeabedPresentation(M5StrategicSeabedMaximumHorizontalSpanMeters, true) ||
        UseStrategicSeabedPresentation(0.0F, true) ||
        UseStrategicSeabedPresentation(M5StrategicSeabedMaximumHorizontalSpanMeters + 1.0F, true))
    {
        return false;
    }

    const auto abyssBands = BuildDeepWaterAbyssPresentationBands(0.60F);
    const auto noAbyssBands = BuildDeepWaterAbyssPresentationBands(1.0F);
    const auto invalidAbyssBands = BuildDeepWaterAbyssPresentationBands(
        (std::numeric_limits<float>::quiet_NaN)());
    if (!abyssBands || abyssBands->size() != 32U || !noAbyssBands || !noAbyssBands->empty() ||
        invalidAbyssBands.has_value() ||
        std::abs(abyssBands->front().viewport.top - 0.60F) > 0.0001F ||
        std::abs(abyssBands->back().viewport.bottom - 1.0F) > 0.0001F ||
        std::abs(abyssBands->front().color.r - M2UnderwaterBackgroundColor.r) > 0.0001F ||
        std::abs(abyssBands->front().color.g - M2UnderwaterBackgroundColor.g) > 0.0001F ||
        std::abs(abyssBands->front().color.b - M2UnderwaterBackgroundColor.b) > 0.0001F)
    {
        return false;
    }
    for (std::size_t index = 1U; index < abyssBands->size(); ++index)
    {
        const auto& previous = (*abyssBands)[index - 1U];
        const auto& current = (*abyssBands)[index];
        if (std::abs(previous.viewport.bottom - current.viewport.top) > 0.0001F ||
            current.color.r > previous.color.r || current.color.g > previous.color.g ||
            current.color.b > previous.color.b ||
            std::abs(current.color.b - previous.color.b) > 0.01F)
        {
            return false;
        }
    }

    // Tactical context preserves the accepted local profile at the centre and descends into deep water
    // away from it; the wide LOD therefore cannot become a flat replacement floor.
    if (M5StrategicSeabedProfile.size() != 28U ||
        M5StrategicSeabedProfile.front().yMeters >= -700.0F ||
        M5StrategicSeabedProfile.back().yMeters >= -700.0F ||
        M5StrategicSeabedProfile[7].xMeters != -400.0F ||
        M5StrategicSeabedProfile[7].yMeters != -160.0F ||
        M5StrategicSeabedProfile[15].xMeters != 0.0F ||
        M5StrategicSeabedProfile[15].yMeters != -220.0F ||
        M5StrategicSeabedProfile[20].xMeters != 400.0F ||
        M5StrategicSeabedProfile[20].yMeters != -165.0F)
    {
        return false;
    }

    const auto strategic = BuildStrategicSeabedPresentationModel();
    if (!strategic || strategic->primitives.size() != 1U ||
        strategic->primitives[0].vertices.size() != 324U ||
        strategic->primitives[0].indices.size() != 486U ||
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
    source.modelToWorld.values[12] = 10.0F;
    source.modelToWorld.values[13] = 20.0F;
    source.modelToWorld.values[14] = 30.0F;
    source.material.baseColorFactor = {0.5F, 0.5F, 0.5F, 1.0F};
    const std::vector<Render::ModelDrawInstance> localDraws = BuildEnvironmentPresentationDraws(
        std::span<const Render::ModelDrawInstance>(&source, 1U),
        std::span<const EnvironmentPresentationTile>(*localTiles));
    if (localDraws.size() != 1U ||
        std::abs(localDraws[0].modelToWorld.values[12] - 10.0F) > 0.001F ||
        std::abs(localDraws[0].modelToWorld.values[13] - 20.0F) > 0.001F ||
        std::abs(localDraws[0].material.baseColorFactor[0] - 0.5F) > 0.001F)
    {
        return false;
    }

    source.material.name = std::string(ScalableEnvironmentPresentationDetail::DefaultM5SuppressedTiledMaterial);
    const auto genericIce = BuildEnvironmentPresentationDraws(
        std::span<const Render::ModelDrawInstance>(&source, 1U),
        std::span<const EnvironmentPresentationTile>(*localTiles));
    const auto scenarioIce = BuildEnvironmentPresentationDraws(
        std::span<const Render::ModelDrawInstance>(&source, 1U),
        std::span<const EnvironmentPresentationTile>(*localTiles),
        true);
    return genericIce.empty() && scenarioIce.size() == 1U;
}
} // namespace DeepRun::Tests
