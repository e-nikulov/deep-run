#pragma once

#include "Engine/Physics/PhysicsTypes.h"

#include <algorithm>
#include <cmath>
#include <expected>
#include <string>
#include <string_view>

namespace DeepRun::Weapons
{
// Weapon-employment limits are deliberately split from weapon kinematics. Publicly documented maxima are used
// where credible open sources exist; minimum range and horizontal off-boresight limits are conservative Game
// policy when no reliable public fire-control limit is available. See docs/design/weapon-employment-envelopes.md.
struct WeaponEmploymentEnvelope final
{
    std::string_view id{};
    float minimumLaunchDepthMeters = 0.0F;
    float maximumLaunchDepthMeters = 0.0F;
    float minimumTargetRangeMeters = 0.0F;
    float maximumTargetRangeMeters = 0.0F;
    float maximumOffBoresightRadians = 0.0F;
    float maximumCarrierSpeedMetersPerSecond = 0.0F;
    float minimumTargetDepthMeters = 0.0F;
    float maximumTargetDepthMeters = 0.0F;
};

struct WeaponEmploymentContext final
{
    Physics::PhysicsVector3 launchPositionMeters{};
    Physics::PhysicsVector3 perceivedTargetPositionMeters{};
    float launchDepthMeters = 0.0F;
    float perceivedTargetDepthMeters = 0.0F;
    float carrierSpeedMetersPerSecond = 0.0F;
    float launcherHeadingRadians = 0.0F;
};

struct WeaponEmploymentAssessment final
{
    bool allowed = false;
    float targetRangeMeters = 0.0F;
    float offBoresightRadians = 0.0F;
    std::string reason{};
};

inline constexpr float DegreesToRadians(const float degrees) noexcept
{
    return degrees * 0.01745329251994329577F;
}

inline constexpr float KnotsToMetersPerSecond(const float knots) noexcept
{
    return knots * 0.5144444444444444F;
}

// USET-80: public maximum range/depth values; Project 949A public carrier-speed envelope.
// The 10 m minimum launch depth, 500 m minimum range and +/-60 degree horizontal sector are Game policy.
inline constexpr WeaponEmploymentEnvelope Uset80EmploymentEnvelope{
    .id = "USET-80",
    .minimumLaunchDepthMeters = 10.0F,
    .maximumLaunchDepthMeters = 400.0F,
    .minimumTargetRangeMeters = 500.0F,
    .maximumTargetRangeMeters = 18'000.0F,
    .maximumOffBoresightRadians = DegreesToRadians(60.0F),
    .maximumCarrierSpeedMetersPerSecond = KnotsToMetersPerSecond(18.0F),
    .minimumTargetDepthMeters = 0.0F,
    .maximumTargetDepthMeters = 1'000.0F};

// 65-76A fast profile. Public sources describe ~50 km at ~50 kt and a Project 949A launch envelope to 480 m
// at carrier speed up to 13 kt. Minimum range, shallow target band and +/-45 degree sector are Game policy.
inline constexpr WeaponEmploymentEnvelope Type6576AFastEmploymentEnvelope{
    .id = "65-76A-fast",
    .minimumLaunchDepthMeters = 10.0F,
    .maximumLaunchDepthMeters = 480.0F,
    .minimumTargetRangeMeters = 1'000.0F,
    .maximumTargetRangeMeters = 50'000.0F,
    .maximumOffBoresightRadians = DegreesToRadians(45.0F),
    .maximumCarrierSpeedMetersPerSecond = KnotsToMetersPerSecond(13.0F),
    .minimumTargetDepthMeters = 0.0F,
    .maximumTargetDepthMeters = 25.0F};

// Public sources also describe a lower-speed 65-76/65-76A profile reaching roughly 100 km. It remains a
// separate envelope so future gameplay must explicitly select the economy profile rather than silently doubling range.
inline constexpr WeaponEmploymentEnvelope Type6576AEconomyEmploymentEnvelope{
    .id = "65-76A-economy",
    .minimumLaunchDepthMeters = 10.0F,
    .maximumLaunchDepthMeters = 480.0F,
    .minimumTargetRangeMeters = 1'000.0F,
    .maximumTargetRangeMeters = 100'000.0F,
    .maximumOffBoresightRadians = DegreesToRadians(45.0F),
    .maximumCarrierSpeedMetersPerSecond = KnotsToMetersPerSecond(13.0F),
    .minimumTargetDepthMeters = 0.0F,
    .maximumTargetDepthMeters = 25.0F};

// P-700/Project 949A public sources consistently describe underwater launch near 30-50 m, a 50 m maximum
// launch depth, carrier speed up to 5 kt and ~550 km maximum range. A 20 km minimum is reported by a secondary
// technical compilation. The +/-90 degree horizontal target sector and shallow surface-target band are Game policy.
inline constexpr WeaponEmploymentEnvelope P700GranitEmploymentEnvelope{
    .id = "P-700-Granit",
    .minimumLaunchDepthMeters = 30.0F,
    .maximumLaunchDepthMeters = 50.0F,
    .minimumTargetRangeMeters = 20'000.0F,
    .maximumTargetRangeMeters = 550'000.0F,
    .maximumOffBoresightRadians = DegreesToRadians(90.0F),
    .maximumCarrierSpeedMetersPerSecond = KnotsToMetersPerSecond(5.0F),
    .minimumTargetDepthMeters = 0.0F,
    .maximumTargetDepthMeters = 25.0F};

[[nodiscard]] inline std::expected<void, std::string> ValidateWeaponEmploymentEnvelope(
    const WeaponEmploymentEnvelope& envelope)
{
    constexpr float pi = 3.14159265358979323846F;
    if (envelope.id.empty() || !std::isfinite(envelope.minimumLaunchDepthMeters) ||
        !std::isfinite(envelope.maximumLaunchDepthMeters) || envelope.minimumLaunchDepthMeters < 0.0F ||
        envelope.maximumLaunchDepthMeters < envelope.minimumLaunchDepthMeters ||
        !std::isfinite(envelope.minimumTargetRangeMeters) || !std::isfinite(envelope.maximumTargetRangeMeters) ||
        envelope.minimumTargetRangeMeters < 0.0F || envelope.maximumTargetRangeMeters <= envelope.minimumTargetRangeMeters ||
        !std::isfinite(envelope.maximumOffBoresightRadians) || envelope.maximumOffBoresightRadians <= 0.0F ||
        envelope.maximumOffBoresightRadians > pi || !std::isfinite(envelope.maximumCarrierSpeedMetersPerSecond) ||
        envelope.maximumCarrierSpeedMetersPerSecond <= 0.0F || !std::isfinite(envelope.minimumTargetDepthMeters) ||
        !std::isfinite(envelope.maximumTargetDepthMeters) || envelope.minimumTargetDepthMeters < 0.0F ||
        envelope.maximumTargetDepthMeters < envelope.minimumTargetDepthMeters)
    {
        return std::unexpected("weapon employment envelope is invalid");
    }
    return {};
}

[[nodiscard]] inline float WrapEmploymentAngle(const float radians) noexcept
{
    return std::remainder(radians, 6.28318530717958647692F);
}

[[nodiscard]] inline WeaponEmploymentAssessment EvaluateWeaponEmployment(
    const WeaponEmploymentEnvelope& envelope,
    const WeaponEmploymentContext& context)
{
    const auto validEnvelope = ValidateWeaponEmploymentEnvelope(envelope);
    if (!validEnvelope)
    {
        return {.allowed = false, .reason = validEnvelope.error()};
    }
    if (!context.launchPositionMeters.IsFinite() || !context.perceivedTargetPositionMeters.IsFinite() ||
        !std::isfinite(context.launchDepthMeters) || !std::isfinite(context.perceivedTargetDepthMeters) ||
        !std::isfinite(context.carrierSpeedMetersPerSecond) || !std::isfinite(context.launcherHeadingRadians) ||
        context.launchDepthMeters < 0.0F || context.perceivedTargetDepthMeters < 0.0F ||
        context.carrierSpeedMetersPerSecond < 0.0F)
    {
        return {.allowed = false, .reason = "weapon employment context is invalid"};
    }

    const float dx = context.perceivedTargetPositionMeters.x - context.launchPositionMeters.x;
    const float dy = context.perceivedTargetPositionMeters.y - context.launchPositionMeters.y;
    const float rangeMeters = static_cast<float>(std::hypot(static_cast<double>(dx), static_cast<double>(dy)));
    const float targetBearingRadians = static_cast<float>(std::atan2(static_cast<double>(dy), static_cast<double>(dx)));
    const float offBoresightRadians = std::abs(WrapEmploymentAngle(targetBearingRadians - context.launcherHeadingRadians));

    const auto rejected = [&](std::string reason) -> WeaponEmploymentAssessment {
        return {.allowed = false,
                .targetRangeMeters = rangeMeters,
                .offBoresightRadians = offBoresightRadians,
                .reason = std::move(reason)};
    };

    if (context.launchDepthMeters < envelope.minimumLaunchDepthMeters)
    {
        return rejected("launch depth is shallower than the weapon envelope");
    }
    if (context.launchDepthMeters > envelope.maximumLaunchDepthMeters)
    {
        return rejected("launch depth exceeds the weapon envelope");
    }
    if (rangeMeters < envelope.minimumTargetRangeMeters)
    {
        return rejected("target is inside the weapon minimum range");
    }
    if (rangeMeters > envelope.maximumTargetRangeMeters)
    {
        return rejected("target is beyond the weapon maximum range");
    }
    if (offBoresightRadians > envelope.maximumOffBoresightRadians)
    {
        return rejected("target bearing is outside the weapon launch sector");
    }
    if (context.carrierSpeedMetersPerSecond > envelope.maximumCarrierSpeedMetersPerSecond)
    {
        return rejected("carrier speed exceeds the weapon launch envelope");
    }
    if (context.perceivedTargetDepthMeters < envelope.minimumTargetDepthMeters ||
        context.perceivedTargetDepthMeters > envelope.maximumTargetDepthMeters)
    {
        return rejected("perceived target depth is outside the weapon target envelope");
    }

    return {.allowed = true,
            .targetRangeMeters = rangeMeters,
            .offBoresightRadians = offBoresightRadians,
            .reason = "weapon employment envelope satisfied"};
}

// Compile-time profile sanity catches accidental changes to public/gameplay boundaries before runtime tests.
static_assert(Uset80EmploymentEnvelope.maximumTargetRangeMeters == 18'000.0F);
static_assert(Uset80EmploymentEnvelope.maximumLaunchDepthMeters == 400.0F);
static_assert(Type6576AFastEmploymentEnvelope.maximumTargetRangeMeters == 50'000.0F);
static_assert(Type6576AEconomyEmploymentEnvelope.maximumTargetRangeMeters == 100'000.0F);
static_assert(P700GranitEmploymentEnvelope.minimumTargetRangeMeters == 20'000.0F);
static_assert(P700GranitEmploymentEnvelope.maximumTargetRangeMeters == 550'000.0F);
static_assert(P700GranitEmploymentEnvelope.maximumLaunchDepthMeters == 50.0F);
} // namespace DeepRun::Weapons
