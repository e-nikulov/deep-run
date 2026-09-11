#pragma once

#include "Game/Environment/ScalableEnvironmentPresentation.h"

#include <cmath>
#include <iostream>

namespace DeepRun::Tests
{
[[nodiscard]] inline bool RunM5ScalableEnvironmentPresentationChecks()
{
    const auto fail = [](const char* message) {
        std::cerr << "M5 scalable environment presentation check failed: " << message << '\n';
        return false;
    };

    if (!Game::HorizontalPresentationBoundsCoverView(-340.0F, 340.0F, 0.0F, 600.0F) ||
        !Game::HorizontalPresentationBoundsCoverView(-340.0F, 340.0F, 0.0F, 680.0F) ||
        Game::HorizontalPresentationBoundsCoverView(-340.0F, 340.0F, 0.0F, 681.0F) ||
        !Game::HorizontalPresentationBoundsCoverView(-320.0F, 320.0F, 0.0F, 640.0F) ||
        Game::HorizontalPresentationBoundsCoverView(-320.0F, 320.0F, 0.0F, 641.0F) ||
        Game::HorizontalPresentationBoundsCoverView(-340.0F, 340.0F, 0.0F, 1'600.0F) ||
        Game::HorizontalPresentationBoundsCoverView(-320.0F, 320.0F, 120.0F, 600.0F))
    {
        return fail("bounded wave/particle detail coverage detection");
    }
    if (!Game::UseDetailedEnvironmentPresentation(600.0F) ||
        !Game::UseDetailedEnvironmentPresentation(2'000.0F) ||
        Game::UseDetailedEnvironmentPresentation(2'001.0F) ||
        Game::UseDetailedEnvironmentPresentation(0.0F))
    {
        return fail("local detailed presentation tier boundary");
    }
    if (Game::UseStrategicSeabedPresentation(2'000.0F) ||
        !Game::UseStrategicSeabedPresentation(3'200.0F) ||
        !Game::UseStrategicSeabedPresentation(3'600.0F) ||
        !Game::UseStrategicSeabedPresentation(12'000.0F) ||
        Game::UseStrategicSeabedPresentation(12'001.0F))
    {
        return fail("strategic seabed presentation span boundary");
    }
    const auto strategicSeabed = Game::BuildStrategicSeabedPresentationModel();
    if (!strategicSeabed || strategicSeabed->materials.size() != 1U ||
        strategicSeabed->primitives.size() != 1U || strategicSeabed->nodes.size() != 1U ||
        strategicSeabed->primitives.front().vertices.size() != 256U ||
        strategicSeabed->primitives.front().indices.size() != 384U ||
        strategicSeabed->bounds.minimum.x != -12'000.0F || strategicSeabed->bounds.maximum.x != 12'000.0F ||
        strategicSeabed->bounds.maximum.y >= 0.0F ||
        strategicSeabed->bounds.minimum.y != Game::M5StrategicSeabedFillBottomYMeters)
    {
        return fail("fixed strategic seabed model contract");
    }

    const auto localTiles = Game::BuildEnvironmentPresentationTiles(-400.0F, 400.0F, 0.0F, 600.0F, -5.0F);
    if (!localTiles || localTiles->size() != 1U || localTiles->front().index != 0 ||
        localTiles->front().offsetXMeters != 0.0F || localTiles->front().offsetYMeters != 0.0F)
    {
        return fail("accepted 600 m local view must remain one authoritative presentation tile");
    }

    const auto initialTacticalTiles = Game::BuildEnvironmentPresentationTiles(
        -400.0F, 400.0F, 0.0F, 1'600.0F, -5.0F);
    if (!initialTacticalTiles || initialTacticalTiles->size() != 3U ||
        initialTacticalTiles->front().index != -1 || initialTacticalTiles->back().index != 1 ||
        std::abs(initialTacticalTiles->front().offsetXMeters + 800.0F) > 0.001F ||
        std::abs(initialTacticalTiles->front().offsetYMeters - 5.0F) > 0.001F ||
        std::abs(initialTacticalTiles->back().offsetXMeters - 800.0F) > 0.001F ||
        std::abs(initialTacticalTiles->back().offsetYMeters + 5.0F) > 0.001F)
    {
        return fail("1.6 km local frame must be covered by three continuous seabed tiles");
    }

    const auto tacticalWideTiles = Game::BuildEnvironmentPresentationTiles(
        -400.0F, 400.0F, 0.0F, 4'000.0F, -5.0F);
    if (!tacticalWideTiles || !tacticalWideTiles->empty())
    {
        return fail("tactical-wide framing must not wallpaper repeated local terrain");
    }

    const auto pannedTiles = Game::BuildEnvironmentPresentationTiles(
        -400.0F, 400.0F, 700.0F, 1'600.0F, -5.0F);
    if (!pannedTiles || pannedTiles->size() != 3U || pannedTiles->front().index != 0 ||
        pannedTiles->back().index != 2)
    {
        return fail("player-directed local pan must move the bounded presentation tile window");
    }

    const auto operationalTiles = Game::BuildEnvironmentPresentationTiles(
        -400.0F, 400.0F, 0.0F, 20'000.0F, -5.0F);
    if (!operationalTiles || !operationalTiles->empty())
    {
        return fail("operational/strategic framing must not multiply local environment geometry");
    }

    Render::ModelDrawInstance baseDraw;
    baseDraw.modelToWorld.values[12] = 10.0F;
    baseDraw.material.baseColorFactor = {0.5F, 0.5F, 0.5F, 1.0F};
    const auto translated = Game::BuildEnvironmentPresentationDraws(
        std::span<const Render::ModelDrawInstance>(&baseDraw, 1U),
        std::span<const Game::EnvironmentPresentationTile>(*initialTacticalTiles));
    if (translated.size() != 3U ||
        std::abs(translated.front().modelToWorld.values[12] + 790.0F) > 0.001F ||
        std::abs(translated.front().modelToWorld.values[13] - 5.0F) > 0.001F ||
        std::abs(translated.back().modelToWorld.values[12] - 810.0F) > 0.001F ||
        std::abs(translated.back().modelToWorld.values[13] + 5.0F) > 0.001F ||
        std::abs(translated[1].material.baseColorFactor[0] - 0.5F) > 0.001F)
    {
        return fail("presentation-only tile transforms and canonical centre material");
    }
    if (std::abs(translated.front().material.baseColorFactor[0] - translated[1].material.baseColorFactor[0]) < 0.001F)
    {
        return fail("repeated local presentation tiles must receive deterministic visual variation");
    }

    Render::ModelDrawInstance iceDraw = baseDraw;
    iceDraw.material.name = "UnderwaterIce";
    const auto genericCombatIce = Game::BuildEnvironmentPresentationDraws(
        std::span<const Render::ModelDrawInstance>(&iceDraw, 1U),
        std::span<const Game::EnvironmentPresentationTile>(*initialTacticalTiles));
    const auto authoredScenarioIce = Game::BuildEnvironmentPresentationDraws(
        std::span<const Render::ModelDrawInstance>(&iceDraw, 1U),
        std::span<const Game::EnvironmentPresentationTile>(*initialTacticalTiles),
        true);
    if (!genericCombatIce.empty() || authoredScenarioIce.size() != initialTacticalTiles->size())
    {
        return fail("generic combat ice gating and explicit scenario opt-in");
    }

    return true;
}
} // namespace DeepRun::Tests
