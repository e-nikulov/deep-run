#pragma once

#include "Simulation/Marine/BuoyancySystem.h"

#include <cstddef>
#include <expected>
#include <optional>
#include <string>

namespace DeepRun::Marine
{
// W1-D does not own structural damage. It produces a bounded, deterministic hydrodynamic impact signal that
// Game can present now and the future M6 damage/crew systems can consume without inventing another wave model.
enum class SurfaceImpactErrorCode
{
    InvalidConfiguration,
    InvalidState,
    InvalidInput,
    NonFiniteResult,
};

struct SurfaceImpactError final
{
    SurfaceImpactErrorCode code = SurfaceImpactErrorCode::InvalidConfiguration;
    std::string message;
};

// Caller-owned tuning. Fractions refer to the same bounded vertical-volume fraction published by buoyancy.
// A point must first emerge below rearmSubmergedFraction, then re-enter through triggerSubmergedFraction.
// This hysteresis prevents one crest from producing an event every fixed tick.
struct SurfaceImpactConfig final
{
    float rearmSubmergedFraction = 0.55F;
    float triggerSubmergedFraction = 0.65F;
    float minimumRelativeWettingSpeedMetersPerSecond = 1.0F;
    float severeRelativeWettingSpeedMetersPerSecond = 6.0F;
};

struct SurfaceImpactPointState final
{
    bool initialized = false;
    bool armed = false;
    float previousSignedDepthMeters = 0.0F;
    float previousSubmergedFraction = 0.0F;
};

struct SurfaceImpactEvent final
{
    std::size_t pointIndex = 0U;
    Physics::PhysicsVector3 worldPositionMeters{};

    // Positive rate at which the local free surface and hull point close in the vertical wetting coordinate.
    // It is derived from authoritative signed-depth change over fixed SimulationTime, so it naturally includes
    // both hull heave/pitch/forward motion and wave motion without a second wave-velocity approximation.
    float relativeWettingSpeedMetersPerSecond = 0.0F;

    // 1/2 rho v^2. This is a dynamic-pressure proxy for impact demand, NOT a claimed local structural peak
    // pressure and NOT damage. M6 can later combine it with authored structural area/resistance contracts.
    float dynamicPressurePascals = 0.0F;

    // Gameplay/presentation-normalized severity. 0 at the configured minimum entry speed, 1 at or above the
    // configured severe entry speed. The dimensional speed and pressure above remain the authoritative evidence.
    float severity = 0.0F;
};

struct SurfaceImpactPointAdvance final
{
    SurfaceImpactPointState nextState{};
    std::optional<SurfaceImpactEvent> event{};
};

// Pure one-point transition. It allocates nothing, mutates nothing, and is independent from PhysicsWorld,
// damage, crew, renderer and input. Game keeps one state per production hull sample and commits nextState only
// after the fixed-tick transaction succeeds.
class SurfaceImpactSystem final
{
public:
    [[nodiscard]] static std::expected<SurfaceImpactPointAdvance, SurfaceImpactError> AdvancePoint(
        const SurfaceImpactConfig& config,
        float waterDensityKgPerCubicMeter,
        std::size_t pointIndex,
        const BuoyancyPointResult& currentPoint,
        const SurfaceImpactPointState& currentState,
        float fixedDeltaSeconds);
};
}
