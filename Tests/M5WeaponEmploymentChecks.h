#pragma once

#include "Game/Submarine/AnteyAcousticModel.h"
#include "Simulation/Acoustics/AcousticWorld.h"
#include "Simulation/Weapons/WeaponEmploymentEnvelope.h"
#include "Tests/D2CombatKnowledgeSalvoChecks.h"

#include <cmath>

namespace DeepRun::Tests
{
[[nodiscard]] inline bool RunM5WeaponEmploymentChecks()
{
    using namespace Weapons;

    const auto at = [](const WeaponEmploymentEnvelope& envelope,
                       const float launchDepth,
                       const float range,
                       const float targetDepth,
                       const float offBoresightDegrees,
                       const float carrierSpeedMetersPerSecond)
    {
        const float radians = DegreesToRadians(offBoresightDegrees);
        return EvaluateWeaponEmployment(envelope, WeaponEmploymentContext{
            .launchPositionMeters = {0.0F, -launchDepth, 0.0F},
            .perceivedTargetPositionMeters = {
                std::cos(radians) * range,
                -launchDepth + std::sin(radians) * range,
                0.0F},
            .launchDepthMeters = launchDepth,
            .perceivedTargetDepthMeters = targetDepth,
            .carrierSpeedMetersPerSecond = carrierSpeedMetersPerSecond,
            .launcherHeadingRadians = 0.0F});
    };

    if (!ValidateWeaponEmploymentEnvelope(Uset80EmploymentEnvelope) ||
        !ValidateWeaponEmploymentEnvelope(Type6576AFastEmploymentEnvelope) ||
        !ValidateWeaponEmploymentEnvelope(Type6576AEconomyEmploymentEnvelope) ||
        !ValidateWeaponEmploymentEnvelope(P700GranitEmploymentEnvelope))
    {
        return false;
    }

    const auto usetNominal = at(Uset80EmploymentEnvelope, 100.0F, 4'000.0F, 120.0F, 20.0F, 2.0F);
    const auto usetTooShallow = at(Uset80EmploymentEnvelope, 9.9F, 4'000.0F, 120.0F, 0.0F, 0.0F);
    const auto usetTooDeep = at(Uset80EmploymentEnvelope, 400.1F, 4'000.0F, 120.0F, 0.0F, 0.0F);
    const auto usetTooClose = at(Uset80EmploymentEnvelope, 100.0F, 499.0F, 120.0F, 0.0F, 0.0F);
    const auto usetTooFar = at(Uset80EmploymentEnvelope, 100.0F, 18'001.0F, 120.0F, 0.0F, 0.0F);
    const auto usetWrongSector = at(Uset80EmploymentEnvelope, 100.0F, 4'000.0F, 120.0F, 61.0F, 0.0F);
    const auto usetTooFast = at(
        Uset80EmploymentEnvelope, 100.0F, 4'000.0F, 120.0F, 0.0F, KnotsToMetersPerSecond(18.1F));
    const auto usetTargetTooDeep = at(Uset80EmploymentEnvelope, 100.0F, 4'000.0F, 1'001.0F, 0.0F, 0.0F);
    if (!usetNominal.allowed || !usetNominal.rangeOptimal || usetTooShallow.allowed || usetTooDeep.allowed ||
        !usetTooClose.allowed || usetTooClose.rangeOptimal || !usetTooFar.allowed || usetTooFar.rangeOptimal ||
        usetWrongSector.allowed || usetTooFast.allowed || usetTargetTooDeep.allowed)
    {
        return false;
    }

    const auto kitFastNominal = at(Type6576AFastEmploymentEnvelope, 100.0F, 25'000.0F, 10.0F, 20.0F, 3.0F);
    const auto kitFastTooFar = at(Type6576AFastEmploymentEnvelope, 100.0F, 50'001.0F, 10.0F, 0.0F, 0.0F);
    const auto kitEconomyFar = at(Type6576AEconomyEmploymentEnvelope, 100.0F, 75'000.0F, 10.0F, 0.0F, 0.0F);
    const auto kitTooDeep = at(Type6576AFastEmploymentEnvelope, 480.1F, 25'000.0F, 10.0F, 0.0F, 0.0F);
    const auto kitWrongSector = at(Type6576AFastEmploymentEnvelope, 100.0F, 25'000.0F, 10.0F, 46.0F, 0.0F);
    const auto kitDeepTarget = at(Type6576AFastEmploymentEnvelope, 100.0F, 25'000.0F, 26.0F, 0.0F, 0.0F);
    if (!kitFastNominal.allowed || !kitFastNominal.rangeOptimal || !kitFastTooFar.allowed || kitFastTooFar.rangeOptimal ||
        !kitEconomyFar.allowed || !kitEconomyFar.rangeOptimal || kitTooDeep.allowed || kitWrongSector.allowed ||
        kitDeepTarget.allowed)
    {
        return false;
    }

    const auto granitNominal = at(P700GranitEmploymentEnvelope, 40.0F, 120'000.0F, 0.0F, 45.0F, 1.0F);
    const auto granitSurfaceLaunch = at(P700GranitEmploymentEnvelope, 0.0F, 120'000.0F, 0.0F, 0.0F, 0.0F);
    const auto granitTooDeep = at(P700GranitEmploymentEnvelope, 50.1F, 120'000.0F, 0.0F, 0.0F, 0.0F);
    const auto granitTooClose = at(P700GranitEmploymentEnvelope, 40.0F, 19'999.0F, 0.0F, 0.0F, 0.0F);
    const auto granitTooFar = at(P700GranitEmploymentEnvelope, 40.0F, 550'001.0F, 0.0F, 0.0F, 0.0F);
    const auto granitWrongSector = at(P700GranitEmploymentEnvelope, 40.0F, 120'000.0F, 0.0F, 91.0F, 0.0F);
    const auto granitTooFast = at(
        P700GranitEmploymentEnvelope, 40.0F, 120'000.0F, 0.0F, 0.0F, KnotsToMetersPerSecond(5.1F));
    const auto granitSubmergedTarget = at(P700GranitEmploymentEnvelope, 40.0F, 120'000.0F, 30.0F, 0.0F, 0.0F);
    if (!granitNominal.allowed || !granitNominal.rangeOptimal || granitSurfaceLaunch.allowed || granitTooDeep.allowed ||
        !granitTooClose.allowed || granitTooClose.rangeOptimal || !granitTooFar.allowed || granitTooFar.rangeOptimal ||
        granitWrongSector.allowed || granitTooFast.allowed || granitSubmergedTarget.allowed)
    {
        return false;
    }

    // Normal-play scenario contract: the player starts at 50 m and the long-range surface combatant starts
    // at 25 km. USET-80 is intentionally beyond its nominal 18 km band but remains fireable; both 65-76A
    // profiles and P-700 start inside their preferred range/depth envelopes.
    const auto normalStartUset = at(Uset80EmploymentEnvelope, 50.0F, 25'000.0F, 2.0F, 0.0F, 0.0F);
    const auto normalStart6576Fast = at(Type6576AFastEmploymentEnvelope, 50.0F, 25'000.0F, 2.0F, 0.0F, 0.0F);
    const auto normalStart6576Economy = at(Type6576AEconomyEmploymentEnvelope, 50.0F, 25'000.0F, 2.0F, 0.0F, 0.0F);
    const auto normalStartP700 = at(P700GranitEmploymentEnvelope, 50.0F, 25'000.0F, 2.0F, 0.0F, 0.0F);
    if (!normalStartUset.allowed || normalStartUset.rangeOptimal || !normalStart6576Fast.allowed ||
        !normalStart6576Fast.rangeOptimal || !normalStart6576Economy.allowed || !normalStart6576Economy.rangeOptimal ||
        !normalStartP700.allowed || !normalStartP700.rangeOptimal)
    {
        return false;
    }

    // The far contact must still be a real passive-acoustic contact, not a presentation-only target marker.
    // Preserve propagation delay: before the ~16.7 s one-way arrival there is no observation; after arrival the
    // 25 km destroyer tuning is detectable, but passive evidence still carries no free range estimate.
    const auto acousticWorld = Acoustics::AcousticWorld::Create({});
    const Acoustics::AcousticSpectrum normalAmbientNoiseDb{.levelDb = {43.0F, 41.0F, 39.0F, 37.0F}};
    const auto anteySnapshot = Game::Submarine::BuildAnteyAcousticSnapshot(
        Game::Submarine::AnteyAcousticRuntimeState{
            .bodyReferencePositionMeters = {0.0F, -50.0F, 0.0F},
            .linearVelocityMetersPerSecond = {},
            .shaftRpm = 0.0F,
            .signedDepthMeters = 50.0F},
        normalAmbientNoiseDb);
    if (!acousticWorld || !anteySnapshot)
    {
        return false;
    }
    const Acoustics::AcousticEmission farDestroyerEmission{
        .positionMeters = {25'000.0F, -2.0F, 0.0F},
        .sourceLevelDb = {.levelDb = {145.0F, 141.0F, 136.0F, 130.0F}},
        .emissionTimeSeconds = 0.0};
    const auto beforeArrival = acousticWorld->CollectPassiveDirectObservation(
        farDestroyerEmission, anteySnapshot->passiveReceiver, 16.0);
    const auto afterArrival = acousticWorld->CollectPassiveDirectObservation(
        farDestroyerEmission, anteySnapshot->passiveReceiver, 17.0);
    if (!beforeArrival || beforeArrival->has_value() || !afterArrival || !afterArrival->has_value() ||
        afterArrival->value().estimatedRangeMeters.has_value())
    {
        return false;
    }

    return RunD2CombatKnowledgeSalvoChecks();
}
} // namespace DeepRun::Tests
