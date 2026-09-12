#pragma once

#include "Simulation/Weapons/WeaponEmploymentEnvelope.h"

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
    if (!usetNominal.allowed || usetTooShallow.allowed || usetTooDeep.allowed || usetTooClose.allowed ||
        usetTooFar.allowed || usetWrongSector.allowed || usetTooFast.allowed || usetTargetTooDeep.allowed)
    {
        return false;
    }

    const auto kitFastNominal = at(Type6576AFastEmploymentEnvelope, 100.0F, 25'000.0F, 10.0F, 20.0F, 3.0F);
    const auto kitFastTooFar = at(Type6576AFastEmploymentEnvelope, 100.0F, 50'001.0F, 10.0F, 0.0F, 0.0F);
    const auto kitEconomyFar = at(Type6576AEconomyEmploymentEnvelope, 100.0F, 75'000.0F, 10.0F, 0.0F, 0.0F);
    const auto kitTooDeep = at(Type6576AFastEmploymentEnvelope, 480.1F, 25'000.0F, 10.0F, 0.0F, 0.0F);
    const auto kitWrongSector = at(Type6576AFastEmploymentEnvelope, 100.0F, 25'000.0F, 10.0F, 46.0F, 0.0F);
    const auto kitDeepTarget = at(Type6576AFastEmploymentEnvelope, 100.0F, 25'000.0F, 26.0F, 0.0F, 0.0F);
    if (!kitFastNominal.allowed || kitFastTooFar.allowed || !kitEconomyFar.allowed || kitTooDeep.allowed ||
        kitWrongSector.allowed || kitDeepTarget.allowed)
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
    return granitNominal.allowed && granitSurfaceLaunch.allowed && !granitTooDeep.allowed && !granitTooClose.allowed &&
           !granitTooFar.allowed && !granitWrongSector.allowed && !granitTooFast.allowed && !granitSubmergedTarget.allowed;
}
} // namespace DeepRun::Tests
