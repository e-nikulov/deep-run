#pragma once

#include "Game/Weapons/P700CarrierLaunchContract.h"
#include "Simulation/Weapons/P700Salvo.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <utility>
#include <vector>

namespace DeepRun::Game::Armament
{
struct P700CommittedSalvoLaunch final
{
    Weapons::P700SalvoRuntimeState runtime{};
    std::vector<std::size_t> launcherSlotIndices{};
};

[[nodiscard]] inline float P700CarrierGameplayHeadingRadians(
    const Submarine::AnteyPhysicalCollisionProxySnapshot& proxy) noexcept
{
    const auto& orientation = proxy.orientation;
    float heading = static_cast<float>(std::atan2(
        2.0 * (static_cast<double>(orientation.w) * orientation.z +
               static_cast<double>(orientation.x) * orientation.y),
        1.0 - 2.0 * (static_cast<double>(orientation.y) * orientation.y +
                     static_cast<double>(orientation.z) * orientation.z)));
    if (proxy.gameplayLongitudinalFacingSign < 0.0F)
    {
        heading = Weapons::WrapEmploymentAngle(heading + 3.14159265358979323846F);
    }
    return heading;
}

[[nodiscard]] inline std::expected<P700CommittedSalvoLaunch, std::string> LaunchProductionP700Salvo(
    const Weapons::P700GranitDefinition& definition,
    const Perception::Track& targetTrack,
    const P700CarrierLaunchContract& carrierContract,
    P700LauncherInventory& inventory,
    const Submarine::AnteyPhysicalCollisionProxySnapshot& playerProxy,
    const float surfaceLevelMeters,
    const float launchDepthMeters,
    const float carrierSpeedMetersPerSecond,
    const Weapons::P700SalvoMode mode,
    const double simulationTimeSeconds,
    const std::uint64_t salvoId)
{
    if (!playerProxy.body.IsValid() || !playerProxy.positionMeters.IsFinite() ||
        !playerProxy.orientation.IsFinite() || playerProxy.turningAround ||
        !std::isfinite(surfaceLevelMeters) || !std::isfinite(launchDepthMeters) || launchDepthMeters < 0.0F ||
        !std::isfinite(carrierSpeedMetersPerSecond) || carrierSpeedMetersPerSecond < 0.0F ||
        !std::isfinite(simulationTimeSeconds) || simulationTimeSeconds < 0.0 || salvoId == 0U)
    {
        return std::unexpected("P-700 production salvo carrier state is invalid");
    }

    const std::size_t missileCount = static_cast<std::size_t>(mode);
    if (missileCount != 1U && missileCount != 2U)
    {
        return std::unexpected("normal P-700 player salvo mode must be Single or Pair");
    }
    const auto slots = inventory.LoadedSlotIndices(missileCount);
    if (!slots)
    {
        return std::unexpected(slots.error());
    }

    const float carrierHeadingRadians = P700CarrierGameplayHeadingRadians(playerProxy);
    std::vector<Weapons::P700CarrierLaunchContext> contexts;
    contexts.reserve(slots->size());
    for (const std::size_t slotIndex : *slots)
    {
        const auto worldAnchor = carrierContract.BuildWorldAnchor(slotIndex, playerProxy);
        if (!worldAnchor)
        {
            return std::unexpected("P-700 salvo production world-anchor composition failed: " + worldAnchor.error());
        }
        contexts.push_back(Weapons::P700CarrierLaunchContext{
            .launchPositionMeters = worldAnchor->positionMeters,
            .launchForwardUnitVector = worldAnchor->forwardUnitVector,
            .surfaceLevelYMeters = surfaceLevelMeters,
            .launchDepthMeters = launchDepthMeters,
            .carrierSpeedMetersPerSecond = carrierSpeedMetersPerSecond,
            .carrierHeadingRadians = carrierHeadingRadians});
    }

    // Materialize every missile before mutating inventory. If any member fails its Track/employment/geometry gate,
    // no launcher is consumed. Only a fully valid salvo commits Loaded -> Spent for all selected slots.
    auto salvo = Weapons::LaunchP700Salvo(
        definition,
        targetTrack,
        contexts,
        simulationTimeSeconds,
        salvoId);
    if (!salvo)
    {
        return std::unexpected(salvo.error());
    }
    const auto consumed = inventory.ConsumeMany(*slots);
    if (!consumed)
    {
        return std::unexpected("P-700 salvo inventory commit failed: " + consumed.error());
    }

    return P700CommittedSalvoLaunch{
        .runtime = std::move(*salvo),
        .launcherSlotIndices = *slots};
}
} // namespace DeepRun::Game::Armament
