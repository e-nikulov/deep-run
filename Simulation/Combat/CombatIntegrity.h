#pragma once

#include "Engine/Physics/PhysicsTypes.h"

#include <algorithm>
#include <cmath>
#include <expected>
#include <string>

namespace DeepRun::Combat
{
// M5-bounded combat durability. This is intentionally not the M6 compartment/system/flooding damage model.
// It gives a physically identified combat body one coarse integrity value so confirmed weapon impacts can have
// an authoritative consequence without inventing subsystem damage early.
struct CombatIntegrityState final
{
    Physics::PhysicsBodyHandle body{};
    float maximumIntegrity = 0.0F;
    float remainingIntegrity = 0.0F;
    bool destroyed = false;
    double lastUpdateTimeSeconds = 0.0;
};

struct CombatDamageEvent final
{
    Physics::PhysicsBodyHandle targetBody{};
    Physics::PhysicsVector3 positionMeters{};
    float damage = 0.0F;
    double simulationTimeSeconds = 0.0;
};

// Authoritative explosion occurrence produced by a weapon/payload after confirmed impact. M5-D does not yet
// turn this into radial subsystem/flooding damage; those richer consumers belong to later bounded slices/M6.
struct CombatExplosionEvent final
{
    Physics::PhysicsVector3 positionMeters{};
    float nominalDamage = 0.0F;
    float radiusMeters = 0.0F;
    double simulationTimeSeconds = 0.0;
};

[[nodiscard]] inline std::expected<CombatIntegrityState, std::string> CreateCombatIntegrity(
    const Physics::PhysicsBodyHandle body,
    const float maximumIntegrity,
    const double simulationTimeSeconds)
{
    if (!body.IsValid() || !std::isfinite(maximumIntegrity) || maximumIntegrity <= 0.0F ||
        !std::isfinite(simulationTimeSeconds))
    {
        return std::unexpected("combat integrity creation input is invalid");
    }

    return CombatIntegrityState{
        .body = body,
        .maximumIntegrity = maximumIntegrity,
        .remainingIntegrity = maximumIntegrity,
        .destroyed = false,
        .lastUpdateTimeSeconds = simulationTimeSeconds};
}

[[nodiscard]] inline std::expected<void, std::string> ApplyCombatDamage(
    CombatIntegrityState& state,
    const CombatDamageEvent& event)
{
    if (!state.body.IsValid() || !event.targetBody.IsValid() || event.targetBody != state.body)
    {
        return std::unexpected("combat damage target does not match integrity owner");
    }
    if (!std::isfinite(state.maximumIntegrity) || state.maximumIntegrity <= 0.0F ||
        !std::isfinite(state.remainingIntegrity) || state.remainingIntegrity < 0.0F ||
        state.remainingIntegrity > state.maximumIntegrity || !event.positionMeters.IsFinite() ||
        !std::isfinite(event.damage) || event.damage <= 0.0F || !std::isfinite(event.simulationTimeSeconds) ||
        event.simulationTimeSeconds < state.lastUpdateTimeSeconds)
    {
        return std::unexpected("combat damage state/input is invalid or time-reversing");
    }

    state.remainingIntegrity = std::max(0.0F, state.remainingIntegrity - event.damage);
    state.destroyed = state.remainingIntegrity <= 0.0F;
    state.lastUpdateTimeSeconds = event.simulationTimeSeconds;
    return {};
}
} // namespace DeepRun::Combat
