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
        Game::HorizontalPresentationBoundsCoverView(-340.0F, 340.0F, 0.0F, 1'600.0F) ||
        Game::HorizontalPresentationBoundsCoverView(-320.0F, 320.0F, 120.0F, 600.0F))
    {
        return fail("bounded wave/particle detail coverage detection");
    }
    if (!Game::UseDetailedEnvironmentPresentation(600.0F) ||
        !Game::UseDetailedEnvironmentPresentation(9'000.0F) ||
        Game::UseDetailedEnvironmentPresentation(9'001.0F) ||
        Game::UseDetailedEnvironmentPresentation(0.0F))
    {
        return fail("detailed tactical presentation tier boundary");
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
        return fail("1.6 km tactical frame must be covered by three continuous seabed tiles");
    }

    const auto launchOverviewTiles = Game::BuildEnvironmentPresentationTiles(
        -400.0F, 400.0F, 0.0F, 4'000.0F, -5.0F);
    if (!launchOverviewTiles || launchOverviewTiles->size() != 7U ||
        launchOverviewTiles->front().index != -3 || launchOverviewTiles->back().index != 3)
    {
        return fail("4 km launch overview presentation coverage");
    }

    const auto pannedTiles = Game::BuildEnvironmentPresentationTiles(
        -400.0F, 400.0F, 1'700.0F, 4'000.0F, -5.0F);
    if (!pannedTiles || pannedTiles->empty() || pannedTiles->front().index != 0 ||
        pannedTiles->back().index != 5)
    {
        return fail("player-directed tactical pan must move the presentation tile window");
    }

    const auto operationalTiles = Game::BuildEnvironmentPresentationTiles(
        -400.0F, 400.0F, 0.0F, 20'000.0F, -5.0F);
    if (!operationalTiles || !operationalTiles->empty())
    {
        return fail("operational/strategic framing must not multiply local environment geometry");
    }

    Render::ModelDrawInstance baseDraw;
    baseDraw.modelToWorld.values[12] = 10.0F;
    const auto translated = Game::BuildEnvironmentPresentationDraws(
        std::span<const Render::ModelDrawInstance>(&baseDraw, 1U),
        std::span<const Game::EnvironmentPresentationTile>(*initialTacticalTiles));
    if (translated.size() != 3U ||
        std::abs(translated.front().modelToWorld.values[12] + 790.0F) > 0.001F ||
        std::abs(translated.front().modelToWorld.values[13] - 5.0F) > 0.001F ||
        std::abs(translated.back().modelToWorld.values[12] - 810.0F) > 0.001F ||
        std::abs(translated.back().modelToWorld.values[13] + 5.0F) > 0.001F)
    {
        return fail("presentation-only tile transforms");
    }

    return true;
}
} // namespace DeepRun::Tests
