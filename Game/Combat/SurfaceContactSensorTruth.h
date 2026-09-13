#pragma once

#include "Engine/Physics/PhysicsTypes.h"
#include "Simulation/Acoustics/AcousticTypes.h"
#include "Simulation/Perception/SensorObservation.h"

#include <cmath>
#include <optional>
#include <span>

namespace DeepRun::Game::Combat
{
// This type deliberately lives below the perception boundary. Sensor simulation may know physical participants
// in order to generate observations; Contact/Track/UI/weapon knowledge must never receive this identity.
enum class SurfaceContactTruthKind
{
    MilitaryCombatant,
    CivilianVessel,
};

struct SurfaceContactSensorTruth final
{
    SurfaceContactTruthKind kind = SurfaceContactTruthKind::MilitaryCombatant;
    Physics::PhysicsBodyHandle body{};
    Acoustics::AcousticEmitter emitter{};
    Perception::ContactClassification visualClassification = Perception::ContactClassification::Unknown;
    float visibleHeightAboveSurfaceMeters = 1.0F;
};

[[nodiscard]] inline float WrapSurfaceContactAngle(const float radians) noexcept
{
    return std::remainder(radians, 6.28318530717958647692F);
}

[[nodiscard]] inline std::optional<SurfaceContactSensorTruth> SelectSurfaceContactTruthByBearing(
    const Physics::PhysicsVector3& sensorPositionMeters,
    const float perceivedBearingRadians,
    const std::span<const SurfaceContactSensorTruth> candidates,
    const float maximumBearingDeltaRadians)
{
    if (!sensorPositionMeters.IsFinite() || !std::isfinite(perceivedBearingRadians) ||
        !std::isfinite(maximumBearingDeltaRadians) || maximumBearingDeltaRadians <= 0.0F)
    {
        return std::nullopt;
    }

    std::optional<SurfaceContactSensorTruth> best{};
    float bestDelta = maximumBearingDeltaRadians;
    for (const auto& candidate : candidates)
    {
        if (!candidate.body.IsValid() || !candidate.emitter.positionMeters.IsFinite() ||
            candidate.visualClassification == Perception::ContactClassification::Unknown ||
            !std::isfinite(candidate.visibleHeightAboveSurfaceMeters) ||
            candidate.visibleHeightAboveSurfaceMeters <= 0.0F)
        {
            continue;
        }
        const float dx = candidate.emitter.positionMeters.x - sensorPositionMeters.x;
        const float dy = candidate.emitter.positionMeters.y - sensorPositionMeters.y;
        const float truthBearing = std::atan2(dy, dx);
        const float delta = std::abs(WrapSurfaceContactAngle(truthBearing - perceivedBearingRadians));
        if (delta <= bestDelta)
        {
            bestDelta = delta;
            best = candidate;
        }
    }
    return best;
}
} // namespace DeepRun::Game::Combat
