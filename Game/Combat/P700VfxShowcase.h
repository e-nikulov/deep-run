#pragma once

#include "Engine/Render/Camera.h"
#include "Game/Weapons/P700LaunchVfx.h"

#include <array>
#include <cstdint>

namespace DeepRun::Game::Combat
{
enum class P700VfxShowcaseEnvironment : std::uint32_t
{
    DayCalm,
    Sunset,
    Night,
    Storm,
};

enum class P700VfxShowcaseCamera : std::uint32_t
{
    UnderwaterWide,
    UnderwaterTrack,
    WaterlineMoneyShot,
    SideBreach,
    Follow,
    SeaSkim,
};

struct P700VfxShowcaseProfile final
{
    const char* name = "DAY_CALM";
    std::uint64_t deterministicSeed = 0x5037303056465801ULL;
    Armament::P700LaunchVfxEnvironment environment{};
    float nominalDurationSeconds = 12.0F;
};

struct P700VfxShowcaseCameraPreset final
{
    const char* name = "UNDERWATER_WIDE";
    P700VfxShowcaseCamera camera = P700VfxShowcaseCamera::UnderwaterWide;
    std::array<float, 3> relativePositionMeters{};
    std::array<float, 3> targetOffsetMeters{};
    float horizontalSpanMeters = 220.0F;
    float activationStartSeconds = 0.0F;
    float activationEndSeconds = 3.8F;
    bool tracksMissile = false;
    bool waterlineLensDroplets = false;
};

[[nodiscard]] constexpr P700VfxShowcaseProfile P700VfxShowcaseProfileFor(
    const P700VfxShowcaseEnvironment environment) noexcept
{
    using Armament::P700LaunchVfxEnvironment;
    switch (environment)
    {
    case P700VfxShowcaseEnvironment::DayCalm:
        return {"DAY_CALM", 0x5037303056465801ULL,
                P700LaunchVfxEnvironment{.significantWaveHeightMeters = 0.20F, .windSpeedMetersPerSecond = 2.0F,
                    .windDirectionRadians = 0.25F, .rainRateMillimetersPerHour = 0.0F,
                    .humidityFraction = 0.68F, .daylightFraction = 1.0F, .lightningVisible = false}, 12.0F};
    case P700VfxShowcaseEnvironment::Sunset:
        return {"SUNSET", 0x5037303056465802ULL,
                P700LaunchVfxEnvironment{.significantWaveHeightMeters = 0.65F, .windSpeedMetersPerSecond = 5.0F,
                    .windDirectionRadians = 0.70F, .rainRateMillimetersPerHour = 0.0F,
                    .humidityFraction = 0.84F, .daylightFraction = 0.38F, .lightningVisible = false}, 12.0F};
    case P700VfxShowcaseEnvironment::Night:
        return {"NIGHT", 0x5037303056465803ULL,
                P700LaunchVfxEnvironment{.significantWaveHeightMeters = 0.85F, .windSpeedMetersPerSecond = 6.0F,
                    .windDirectionRadians = 1.15F, .rainRateMillimetersPerHour = 0.0F,
                    .humidityFraction = 0.90F, .daylightFraction = 0.0F, .lightningVisible = false}, 12.0F};
    case P700VfxShowcaseEnvironment::Storm:
        return {"STORM", 0x5037303056465804ULL,
                P700LaunchVfxEnvironment{.significantWaveHeightMeters = 6.5F, .windSpeedMetersPerSecond = 21.0F,
                    .windDirectionRadians = 1.90F, .rainRateMillimetersPerHour = 34.0F,
                    .humidityFraction = 0.98F, .daylightFraction = 0.14F, .lightningVisible = true}, 12.0F};
    }
    return {};
}

inline constexpr std::array<P700VfxShowcaseCameraPreset, 6> P700VfxShowcaseCameras{{
    {"UNDERWATER_WIDE", P700VfxShowcaseCamera::UnderwaterWide,
        {-115.0F, -20.0F, 115.0F}, {8.0F, -18.0F, 0.0F}, 230.0F, 0.0F, 4.0F, false, false},
    {"UNDERWATER_TRACK", P700VfxShowcaseCamera::UnderwaterTrack,
        {-30.0F, -13.0F, 34.0F}, {2.0F, 0.0F, 0.0F}, 72.0F, 2.0F, 5.0F, true, false},
    {"WATERLINE_MONEY_SHOT", P700VfxShowcaseCamera::WaterlineMoneyShot,
        {-42.0F, 1.0F, 52.0F}, {7.0F, 0.0F, 0.0F}, 88.0F, 3.6F, 6.5F, false, true},
    {"SIDE_BREACH", P700VfxShowcaseCamera::SideBreach,
        {-105.0F, 14.0F, 130.0F}, {18.0F, 13.0F, 0.0F}, 150.0F, 3.8F, 7.4F, true, false},
    {"FOLLOW", P700VfxShowcaseCamera::Follow,
        {-24.0F, 8.0F, 24.0F}, {12.0F, 2.0F, 0.0F}, 62.0F, 5.0F, 9.2F, true, false},
    {"SEA_SKIM", P700VfxShowcaseCamera::SeaSkim,
        {55.0F, 1.2F, 0.0F}, {0.0F, 4.5F, 0.0F}, 92.0F, 7.0F, 12.0F, true, false},
}};

// Visual-only replay guidance. It is deliberately data, not a gameplay time-scale authority. A replay/photo
// mode may consume this later; normal simulation must continue at 100%.
struct P700VfxShowcaseSpeedRampPoint final
{
    float timeSeconds = 0.0F;
    float presentationRate = 1.0F;
};

inline constexpr std::array<P700VfxShowcaseSpeedRampPoint, 7> P700VfxShowcaseSpeedRamp{{
    {0.0F, 1.0F},
    {3.8F, 1.0F},
    {4.25F, 0.42F},
    {5.15F, 1.0F},
    {5.85F, 0.66F},
    {6.65F, 1.0F},
    {12.0F, 1.0F},
}};
} // namespace DeepRun::Game::Combat
