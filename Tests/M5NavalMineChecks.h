#pragma once

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

    return physicsWorld.DestroyBody(targetBody);
}
} // namespace DeepRun::Tests
