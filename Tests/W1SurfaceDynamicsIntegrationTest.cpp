#include "Engine/Diagnostics/Logger.h"
#include "Engine/Physics/PhysicsWorld.h"
#include "Simulation/Marine/BuoyancySystem.h"
#include "Simulation/Marine/SurfaceImpactSystem.h"
#include "Tests/W1SurfaceVesselDynamicsChecks.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <vector>

namespace
{
constexpr float FixedDeltaSeconds = 1.0F / 60.0F;
constexpr int FixedTickCount = 1'200;
constexpr float SurfaceMassKg = 14'820'000.0F;
constexpr float PointPotentialVolumeCubicMeters = 4'696.0F;
constexpr float PointHalfHeightMeters = 4.35F;
constexpr DeepRun::Marine::SurfaceImpactConfig ImpactConfig{
    .rearmSubmergedFraction = 0.55F,
    .triggerSubmergedFraction = 0.65F,
    .minimumRelativeWettingSpeedMetersPerSecond = 1.0F,
    .severeRelativeWettingSpeedMetersPerSecond = 6.0F};

float PitchRadians(const DeepRun::Physics::PhysicsQuaternion& orientation) noexcept
{
    return 2.0F * std::atan2(orientation.z, orientation.w);
}

bool ApplyPointForces(
    DeepRun::Physics::PhysicsWorld& physics,
    const DeepRun::Physics::PhysicsBodyHandle body,
    const DeepRun::Marine::BuoyancyResult& result)
{
    for (const auto& point : result.points)
    {
        DeepRun::Physics::PhysicsError error;
        if (!physics.AddForceAtWorldPosition(body, point.forceNewtons, point.worldPositionMeters, &error))
        {
            std::cerr << "force application failed: " << error.message << '\n';
            return false;
        }
    }
    return true;
}
}

int main()
{
    using namespace DeepRun;
    const auto water = Tests::W1SurfaceVesselDynamicsDetail::BuildWater();
    if (!water)
    {
        std::cerr << "water creation failed: " << water.error() << '\n';
        return 1;
    }
    const Marine::BuoyancyComponent component = Tests::W1SurfaceVesselDynamicsDetail::BuildLongHullBuoyancy();

    Diagnostics::Logger logger;
    Physics::PhysicsWorld physics(logger);
    if (!physics.Initialize())
    {
        std::cerr << "PhysicsWorld initialization failed\n";
        return 1;
    }
    const auto gravity = physics.Gravity();
    if (!gravity || !gravity->IsFinite() || gravity->y >= 0.0F ||
        std::abs(gravity->x) > 1.0e-6F || std::abs(gravity->z) > 1.0e-6F)
    {
        std::cerr << "authoritative gravity is unavailable or not vertical\n";
        return 1;
    }
    const float gravityMagnitudeMetersPerSecondSquared = -gravity->y;

    const float fullPotentialVolume = PointPotentialVolumeCubicMeters * static_cast<float>(component.points.size());
    const float equilibriumFraction =
        SurfaceMassKg / (water->Config().densityKgPerCubicMeter * fullPotentialVolume);
    const float equilibriumDepthMeters = PointHalfHeightMeters * (2.0F * equilibriumFraction - 1.0F);
    if (!std::isfinite(equilibriumDepthMeters) || equilibriumDepthMeters <= 0.0F || equilibriumDepthMeters >= PointHalfHeightMeters)
    {
        std::cerr << "invalid surface equilibrium depth\n";
        return 1;
    }

    Physics::PhysicsDegreesOfFreedom planar;
    planar.translationX = false;
    planar.translationY = true;
    planar.translationZ = false;
    planar.rotationX = false;
    planar.rotationY = false;
    planar.rotationZ = true;

    Physics::DynamicBoxBodyCreateInfo bodyInfo;
    bodyInfo.halfExtents = {50.0F, PointHalfHeightMeters, 2.0F};
    bodyInfo.mass = SurfaceMassKg;
    bodyInfo.position = {0.0F, -equilibriumDepthMeters, 0.0F};
    bodyInfo.gravityEnabled = true;
    bodyInfo.linearDamping = 0.18F;
    bodyInfo.angularDamping = 0.35F;
    bodyInfo.degreesOfFreedom = planar;

    Physics::PhysicsError error;
    const Physics::PhysicsBodyHandle waveBody = physics.CreateDynamicBoxBody(bodyInfo, &error);
    if (!waveBody.IsValid())
    {
        std::cerr << "wave body creation failed: " << error.message << '\n';
        return 1;
    }
    bodyInfo.position.x = 300.0F;
    const Physics::PhysicsBodyHandle flatBody = physics.CreateDynamicBoxBody(bodyInfo, &error);
    if (!flatBody.IsValid())
    {
        std::cerr << "flat body creation failed: " << error.message << '\n';
        return 1;
    }

    Marine::BuoyancyResult waveResult;
    waveResult.points.reserve(component.points.size());
    std::vector<Marine::SurfaceImpactPointState> impactStates(component.points.size());
    std::size_t impactEventCount = 0U;
    float maximumImpactSpeedMetersPerSecond = 0.0F;
    float maximumImpactPressurePascals = 0.0F;
    float maximumImpactSeverity = 0.0F;
    float waveMinimumY = -equilibriumDepthMeters;
    float waveMaximumY = -equilibriumDepthMeters;
    float maximumWavePitch = 0.0F;
    float maximumFlatHeave = 0.0F;
    float maximumFlatPitch = 0.0F;

    for (int tick = 0; tick < FixedTickCount; ++tick)
    {
        const auto waveState = physics.GetBodyState(waveBody);
        const auto flatState = physics.GetBodyState(flatBody);
        if (!waveState || !flatState)
        {
            std::cerr << "body state unavailable\n";
            return 1;
        }

        const double simulationTimeSeconds = static_cast<double>(tick) * FixedDeltaSeconds;
        const auto waveCalculated = Marine::BuoyancySystem::CalculateWaveHydrostatic(
            *water,
            component,
            {.worldPositionMeters = waveState->position, .worldOrientation = waveState->orientation},
            gravityMagnitudeMetersPerSecondSquared,
            simulationTimeSeconds,
            waveResult);
        const auto flatCalculated = Marine::BuoyancySystem::Calculate(
            *water,
            component,
            {.worldPositionMeters = flatState->position, .worldOrientation = flatState->orientation},
            gravityMagnitudeMetersPerSecondSquared);
        if (!waveCalculated || !flatCalculated)
        {
            std::cerr << "buoyancy evaluation failed\n";
            return 1;
        }

        // W1-D integration evidence comes from the same wave-aware samples that drive W1-C hydrostatics.
        // No synthetic impact pulse is injected: the detector observes only the rigid body's actual motion
        // against the deterministic Beaufort-7 spectrum over fixed SimulationTime.
        for (std::size_t index = 0U; index < waveResult.points.size(); ++index)
        {
            const auto& point = waveResult.points[index];
            const auto impact = Marine::SurfaceImpactSystem::AdvancePoint(
                ImpactConfig,
                water->Config().densityKgPerCubicMeter,
                index,
                {.worldXMeters = point.worldPositionMeters.x,
                 .worldYMeters = point.worldPositionMeters.y,
                 .worldZMeters = point.worldPositionMeters.z,
                 .signedDepthMeters = point.signedDepthMeters,
                 .submergedFraction = point.submergedFraction},
                impactStates[index],
                FixedDeltaSeconds);
            if (!impact)
            {
                std::cerr << "surface-impact evaluation failed: " << impact.error().message << '\n';
                return 1;
            }
            impactStates[index] = impact->nextState;
            if (impact->event)
            {
                ++impactEventCount;
                maximumImpactSpeedMetersPerSecond = (std::max)(
                    maximumImpactSpeedMetersPerSecond,
                    impact->event->relativeWettingSpeedMetersPerSecond);
                maximumImpactPressurePascals = (std::max)(
                    maximumImpactPressurePascals,
                    impact->event->dynamicPressurePascals);
                maximumImpactSeverity = (std::max)(maximumImpactSeverity, impact->event->severity);
            }
        }

        if (!ApplyPointForces(physics, waveBody, waveResult) ||
            !ApplyPointForces(physics, flatBody, *flatCalculated))
        {
            std::cerr << "buoyancy force application failed\n";
            return 1;
        }

        physics.Step(FixedDeltaSeconds);
        const auto steppedWave = physics.GetBodyState(waveBody);
        const auto steppedFlat = physics.GetBodyState(flatBody);
        if (!steppedWave || !steppedFlat)
            return 1;

        waveMinimumY = (std::min)(waveMinimumY, steppedWave->position.y);
        waveMaximumY = (std::max)(waveMaximumY, steppedWave->position.y);
        maximumWavePitch = (std::max)(maximumWavePitch, std::abs(PitchRadians(steppedWave->orientation)));
        maximumFlatHeave = (std::max)(maximumFlatHeave,
            std::abs(steppedFlat->position.y + equilibriumDepthMeters));
        maximumFlatPitch = (std::max)(maximumFlatPitch, std::abs(PitchRadians(steppedFlat->orientation)));
    }

    const float waveHeaveRange = waveMaximumY - waveMinimumY;
    std::cout << "W1-C evidence: equilibrium depth=" << equilibriumDepthMeters
              << " m, wave heave range=" << waveHeaveRange
              << " m, max wave pitch=" << maximumWavePitch
              << " rad, flat max heave=" << maximumFlatHeave
              << " m, flat max pitch=" << maximumFlatPitch << " rad\n";
    std::cout << "W1-D evidence: impacts=" << impactEventCount
              << ", max wetting speed=" << maximumImpactSpeedMetersPerSecond
              << " m/s, max q=" << maximumImpactPressurePascals
              << " Pa, max severity=" << maximumImpactSeverity << '\n';

    if (!(waveHeaveRange > 0.05F) || !(maximumWavePitch > 0.005F) ||
        waveHeaveRange > 12.0F || maximumWavePitch > 0.70F ||
        maximumFlatHeave > 0.02F || maximumFlatPitch > 0.001F)
    {
        std::cerr << "W1-C integrated surface-vessel response gate failed\n";
        return 1;
    }

    if (impactEventCount == 0U ||
        !(maximumImpactSpeedMetersPerSecond >= ImpactConfig.minimumRelativeWettingSpeedMetersPerSecond) ||
        !(maximumImpactPressurePascals > 0.0F) ||
        !std::isfinite(maximumImpactSeverity) || maximumImpactSeverity < 0.0F || maximumImpactSeverity > 1.0F)
    {
        std::cerr << "W1-D integrated wave-slam evidence gate failed\n";
        return 1;
    }

    std::cout << "W1-C/W1-D integrated surface dynamics: PASS\n";
    return 0;
}
