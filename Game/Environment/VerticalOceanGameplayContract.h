#pragma once

namespace DeepRun::Game
{
// D0 Vertical Ocean Gameplay Contract. These values describe the normal visible/playable ocean presentation
// envelope. They are not vessel-specific dive/crush-depth limits and must never be used as a substitute for a
// submarine's own structural/runtime contract.
inline constexpr float NormalGameplaySeaSurfaceReferenceYMeters = 0.0F;
inline constexpr float NormalGameplayMaximumVisibleDepthMeters = 700.0F;
inline constexpr float NormalGameplayOceanBottomReferenceYMeters =
    NormalGameplaySeaSurfaceReferenceYMeters - NormalGameplayMaximumVisibleDepthMeters;

inline constexpr float NormalGameplayPeriscopeZoneMaximumDepthMeters = 20.0F;
inline constexpr float NormalGameplayShallowZoneMaximumDepthMeters = 100.0F;
inline constexpr float NormalGameplayPrimaryCombatZoneMaximumDepthMeters = 300.0F;
inline constexpr float NormalGameplayDeepTacticalZoneMaximumDepthMeters = 500.0F;
inline constexpr float NormalGameplayExtremeZoneMaximumDepthMeters = 600.0F;
inline constexpr float NormalGameplayLowerBoundaryMaximumDepthMeters =
    NormalGameplayMaximumVisibleDepthMeters;

[[nodiscard]] constexpr bool IsWithinNormalGameplayDepthMeters(const float depthMeters) noexcept
{
    return depthMeters >= 0.0F && depthMeters <= NormalGameplayMaximumVisibleDepthMeters;
}

[[nodiscard]] constexpr bool IsWithinNormalGameplayReferenceYMeters(const float worldYMeters) noexcept
{
    return worldYMeters <= NormalGameplaySeaSurfaceReferenceYMeters &&
           worldYMeters >= NormalGameplayOceanBottomReferenceYMeters;
}

// Runtime scenarios may place the authoritative WaterBody surface at a non-zero world Y. The D0 depth contract
// remains relative to that surface and is therefore safe to query without baking the reference Y=0 into physics.
[[nodiscard]] constexpr bool IsWithinNormalGameplayWaterColumn(
    const float worldYMeters,
    const float authoritativeSurfaceYMeters) noexcept
{
    return IsWithinNormalGameplayDepthMeters(authoritativeSurfaceYMeters - worldYMeters);
}

static_assert(NormalGameplayOceanBottomReferenceYMeters == -700.0F);
static_assert(NormalGameplayPeriscopeZoneMaximumDepthMeters < NormalGameplayShallowZoneMaximumDepthMeters);
static_assert(NormalGameplayShallowZoneMaximumDepthMeters < NormalGameplayPrimaryCombatZoneMaximumDepthMeters);
static_assert(NormalGameplayPrimaryCombatZoneMaximumDepthMeters < NormalGameplayDeepTacticalZoneMaximumDepthMeters);
static_assert(NormalGameplayDeepTacticalZoneMaximumDepthMeters < NormalGameplayExtremeZoneMaximumDepthMeters);
static_assert(NormalGameplayExtremeZoneMaximumDepthMeters < NormalGameplayLowerBoundaryMaximumDepthMeters);
} // namespace DeepRun::Game
