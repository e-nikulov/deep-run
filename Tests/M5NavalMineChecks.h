#pragma once

#include "Game/Combat/CombatPlaygroundPresentation.h"
#include "Game/Combat/CombatPlaygroundRuntime.h"
#include "Game/Submarine/AnteyAcousticModel.h"
#include "Game/Submarine/AnteyPhysicalCollisionProxy.h"
#include "Simulation/Weapons/NavalMine.h"

#include <cmath>

namespace DeepRun::Tests
{
[[nodiscard]] inline bool RunM5NavalMineChecks(Physics::PhysicsWorld& physicsWorld)
{
    using namespace Weapons;

    if (!physicsWorld.IsInitialized())
    {
        return false;
    }

    const NavalMineDefinition definition{
        .id = "m5.contact-mine",
        .collisionHalfExtentsMeters = {.x = 1.0F, .y = 1.0F, .z = 1.0F},
        .contactDamage = 80.0F,
        .explosionRadiusMeters = 10.0F};
    auto mineResult = CreateNavalMineRuntime(
        definition, physicsWorld, {.x = 0.0F, .y = -40.0F, .z = 0.0F}, 0.0);
    if (!mineResult)
    {
        return false;
    }
    auto mine = *mineResult;
    const auto originalMineBody = mine.body;

    const auto targetBody = physicsWorld.CreateStaticBoxBody(
        Physics::StaticBoxBodyCreateInfo{
            .halfExtents = {.x = 1.0F, .y = 1.0F, .z = 1.0F},
            .position = {.x = -20.0F, .y = -40.0F, .z = 0.0F}});
    const auto blockerBody = physicsWorld.CreateStaticBoxBody(
        Physics::StaticBoxBodyCreateInfo{
            .halfExtents = {.x = 1.0F, .y = 1.0F, .z = 1.0F},
            .position = {.x = -8.0F, .y = -40.0F, .z = 0.0F}});
    if (!targetBody.IsValid() || !blockerBody.IsValid())
    {
        return false;
    }

    auto integrityResult = Combat::CreateCombatIntegrity(targetBody, 100.0F, 0.0);
    if (!integrityResult)
    {
        return false;
    }
    auto integrity = *integrityResult;

    const Physics::PhysicsBoxSweepQuery sweep{
        .halfExtentsMeters = {.x = 1.0F, .y = 1.0F, .z = 1.0F},
        .startPositionMeters = {.x = -20.0F, .y = -40.0F, .z = 0.0F},
        .orientation = {},
        .displacementMeters = {.x = 30.0F, .y = 0.0F, .z = 0.0F}};

    // A closer unrelated physical body must block the closest sweep. Mine-side logic may not skip it in order
    // to manufacture a detonation against the later mine body.
    const auto blocked = AdvanceNavalMineAgainstSweep(
        definition, mine, targetBody, sweep, physicsWorld, 1.0);
    if (!blocked || blocked->has_value() || !mine.armed || mine.detonated || mine.body != originalMineBody)
    {
        return false;
    }
    if (!physicsWorld.DestroyBody(blockerBody))
    {
        return false;
    }

    const auto detonation = AdvanceNavalMineAgainstSweep(
        definition, mine, targetBody, sweep, physicsWorld, 2.0);
    if (!detonation || !detonation->has_value() || mine.armed || !mine.detonated || mine.body.IsValid() ||
        physicsWorld.GetBodyState(originalMineBody).has_value())
    {
        return false;
    }

    const auto& event = **detonation;
    if (event.damage.targetBody != targetBody || event.damage.damage != definition.contactDamage ||
        event.explosion.nominalDamage != definition.contactDamage ||
        event.explosion.radiusMeters != definition.explosionRadiusMeters ||
        std::abs(event.damage.positionMeters.x + 2.0F) > 0.1F ||
        event.damage.positionMeters != event.explosion.positionMeters ||
        event.damage.simulationTimeSeconds != 2.0 || event.explosion.simulationTimeSeconds != 2.0)
    {
        return false;
    }
    if (!Combat::ApplyCombatDamage(integrity, event.damage) ||
        std::abs(integrity.remainingIntegrity - 20.0F) > 0.001F || integrity.destroyed)
    {
        return false;
    }

    // A consumed mine is idempotent: later evaluation yields no second damage/explosion event, and time still
    // advances monotonically. A subsequent time reversal remains invalid even though no physical body remains.
    const auto repeated = AdvanceNavalMineAgainstSweep(
        definition, mine, targetBody, sweep, physicsWorld, 3.0);
    if (!repeated || repeated->has_value() ||
        AdvanceNavalMineAgainstSweep(definition, mine, targetBody, sweep, physicsWorld, 2.5))
    {
        return false;
    }

    if (!physicsWorld.DestroyBody(targetBody))
    {
        return false;
    }

    // M5-I.2: prove the existing physical mine contract is actually bound into the live combat runtime.
    const Physics::PhysicsVector3 liveStart{.x = 0.0F, .y = -100.0F, .z = 0.0F};
    const Physics::PhysicsVector3 liveHalfExtents{.x = 10.0F, .y = 5.0F, .z = 5.0F};
    const auto livePlayerBody = physicsWorld.CreateStaticBoxBody(
        Physics::StaticBoxBodyCreateInfo{.halfExtents = liveHalfExtents, .position = liveStart});
    if (!livePlayerBody.IsValid())
    {
        return false;
    }
    const auto initialAcoustic = Game::Submarine::BuildAnteyAcousticSnapshot(
        Game::Submarine::AnteyAcousticRuntimeState{
            .bodyReferencePositionMeters = liveStart,
            .linearVelocityMetersPerSecond = {},
            .shaftRpm = 0.0F,
            .signedDepthMeters = 100.0F},
        Acoustics::AcousticSpectrum{.levelDb = {43.0F, 41.0F, 39.0F, 37.0F}});
    auto liveRuntimeResult = Game::Combat::CombatPlaygroundRuntime::Create(physicsWorld, 0.0F, 10.0);
    if (!initialAcoustic || !liveRuntimeResult)
    {
        (void)physicsWorld.DestroyBody(livePlayerBody);
        return false;
    }
    auto liveRuntime = std::move(*liveRuntimeResult);
    const auto bound = liveRuntime.BindPlayerPhysicalProxy(
        Game::Submarine::AnteyPhysicalCollisionProxySnapshot{
            .body = livePlayerBody,
            .positionMeters = liveStart,
            .orientation = {},
            .halfExtentsMeters = liveHalfExtents},
        *initialAcoustic,
        10.0);
    if (!bound || !liveRuntime.Mine() || !liveRuntime.Mine()->armed || !liveRuntime.PlayerIntegrity())
    {
        (void)physicsWorld.DestroyBody(liveRuntime.Destroyer().body);
        (void)physicsWorld.DestroyBody(livePlayerBody);
        return false;
    }

    const auto armedPresentation = Game::Combat::BuildCombatPlaygroundPresentationSnapshot(
        liveRuntime, physicsWorld, 10.0);
    if (!armedPresentation || !armedPresentation->navalMine.has_value())
    {
        (void)physicsWorld.DestroyBody(liveRuntime.Destroyer().body);
        (void)physicsWorld.DestroyBody(livePlayerBody);
        return false;
    }

    const Physics::PhysicsVector3 liveEnd{.x = 600.0F, .y = -135.0F, .z = 0.0F};
    const auto movedAcoustic = Game::Submarine::BuildAnteyAcousticSnapshot(
        Game::Submarine::AnteyAcousticRuntimeState{
            .bodyReferencePositionMeters = liveEnd,
            .linearVelocityMetersPerSecond = {.x = 20.0F, .y = -1.0F, .z = 0.0F},
            .shaftRpm = 80.0F,
            .signedDepthMeters = 135.0F},
        Acoustics::AcousticSpectrum{.levelDb = {43.0F, 41.0F, 39.0F, 37.0F}});
    const auto movedProxy = Game::Submarine::AnteyPhysicalCollisionProxySnapshot{
        .body = livePlayerBody,
        .positionMeters = liveEnd,
        .orientation = {},
        .halfExtentsMeters = liveHalfExtents};
    const auto physicalUpdate = movedAcoustic
        ? liveRuntime.UpdatePlayerPhysicalProxy(movedProxy, *movedAcoustic)
        : std::expected<void, std::string>{std::unexpected("fixture acoustic snapshot failed")};
    const auto liveFrame = physicalUpdate ? liveRuntime.Advance(*movedAcoustic, 11.0)
                                          : std::expected<Game::Combat::CombatPlaygroundFrame, std::string>{
                                                std::unexpected("fixture physical proxy update failed")};
    const bool liveAccepted = liveFrame && liveFrame->playerMineDetonation.has_value() &&
        std::abs(liveFrame->playerIntegrityFraction - 0.20F) <= 0.001F && !liveFrame->playerDestroyed &&
        liveRuntime.Mine() && liveRuntime.Mine()->detonated && !liveRuntime.Mine()->body.IsValid() &&
        liveRuntime.PlayerIntegrity() &&
        std::abs(liveRuntime.PlayerIntegrity()->remainingIntegrity - 20.0F) <= 0.001F &&
        liveRuntime.LastExplosion().has_value();
    const auto consumedPresentation = Game::Combat::BuildCombatPlaygroundPresentationSnapshot(
        liveRuntime, physicsWorld, 11.0);
    const bool presentationAccepted = consumedPresentation && !consumedPresentation->navalMine.has_value() &&
        consumedPresentation->explosion.has_value();

    const bool cleanupDestroyer = physicsWorld.DestroyBody(liveRuntime.Destroyer().body);
    const bool cleanupPlayer = physicsWorld.DestroyBody(livePlayerBody);
    return liveAccepted && presentationAccepted && cleanupDestroyer && cleanupPlayer;
}
} // namespace DeepRun::Tests
