#pragma once

#include <cmath>

namespace DeepRun::Game
{
// D0 Ocean Vertical Presentation Contract. The 700 m value is the normal submarine gameplay band, not an
// ocean-bottom constraint and not a vessel-specific dive/crush-depth limit.
inline constexpr float NormalGameplaySeaSurfaceReferenceYMeters = 0.0F;
inline constexpr float NormalGameplayMaximumVisibleDepthMeters = 700.0F;
inline constexpr float NormalGameplayBandBottomReferenceYMeters =
    NormalGameplaySeaSurfaceReferenceYMeters - NormalGameplayMaximumVisibleDepthMeters;
// Compatibility name retained for existing callers. It means gameplay-band bottom, never physical seabed.
inline constexpr float NormalGameplayOceanBottomReferenceYMeters = NormalGameplayBandBottomReferenceYMeters;

inline constexpr float NormalGameplayPeriscopeZoneMaximumDepthMeters = 20.0F;
inline constexpr float NormalGameplayShallowZoneMaximumDepthMeters = 100.0F;
inline constexpr float NormalGameplayPrimaryCombatZoneMaximumDepthMeters = 300.0F;
inline constexpr float NormalGameplayDeepTacticalZoneMaximumDepthMeters = 500.0F;
inline constexpr float NormalGameplayExtremeZoneMaximumDepthMeters = 600.0F;
inline constexpr float NormalGameplayLowerBoundaryMaximumDepthMeters = NormalGameplayMaximumVisibleDepthMeters;

// Presentation composition policy. Local play prioritizes underwater space; tactical/operational/strategic
// framing progressively reserves more screen area above the surface for future aircraft/helicopter threats.
inline constexpr float NormalGameplayAboveWaterFraction = 0.15F;
inline constexpr float TacticalGameplayAboveWaterFraction = 0.32F;
inline constexpr float OperationalGameplayAboveWaterFraction = 0.36F;
inline constexpr float StrategicGameplayAboveWaterFraction = 0.40F;
inline constexpr float LocalCompositionReferenceHorizontalSpanMeters = 800.0F;
inline constexpr float TacticalCompositionHorizontalSpanMeters = 2'300.0F;
inline constexpr float OperationalCompositionHorizontalSpanMeters = 9'000.0F;
inline constexpr float StrategicCompositionHorizontalSpanMeters = 120'000.0F;

enum class BathymetryDepthBand
{
    Shelf,
    Continental,
    DeepOcean,
    Abyssal,
};

[[nodiscard]] constexpr bool IsWithinNormalGameplayDepthMeters(const float depthMeters) noexcept
{
    return depthMeters >= 0.0F && depthMeters <= NormalGameplayMaximumVisibleDepthMeters;
}

[[nodiscard]] constexpr bool IsWithinNormalGameplayReferenceYMeters(const float worldYMeters) noexcept
{
    return worldYMeters <= NormalGameplaySeaSurfaceReferenceYMeters &&
           worldYMeters >= NormalGameplayBandBottomReferenceYMeters;
}

[[nodiscard]] constexpr bool IsWithinNormalGameplayWaterColumn(
    const float worldYMeters,
    const float authoritativeSurfaceYMeters) noexcept
{
    return IsWithinNormalGameplayDepthMeters(authoritativeSurfaceYMeters - worldYMeters);
}

[[nodiscard]] constexpr BathymetryDepthBand ClassifyBathymetryDepthMeters(const float depthMeters) noexcept
{
    if (depthMeters <= 300.0F)
    {
        return BathymetryDepthBand::Shelf;
    }
    if (depthMeters <= NormalGameplayMaximumVisibleDepthMeters)
    {
        return BathymetryDepthBand::Continental;
    }
    if (depthMeters <= 2'000.0F)
    {
        return BathymetryDepthBand::DeepOcean;
    }
    return BathymetryDepthBand::Abyssal;
}

[[nodiscard]] inline float AboveWaterFractionForPresentationSpanMeters(const float horizontalSpanMeters) noexcept
{
    if (!std::isfinite(horizontalSpanMeters) || horizontalSpanMeters <= 0.0F)
    {
        return NormalGameplayAboveWaterFraction;
    }
    const auto smoothTransition = [](const float value, const float begin, const float end,
                                     const float beginFraction, const float endFraction) noexcept
    {
        const float linear = std::clamp((value - begin) / (end - begin), 0.0F, 1.0F);
        const float smooth = linear * linear * (3.0F - 2.0F * linear);
        return beginFraction + (endFraction - beginFraction) * smooth;
    };

    if (horizontalSpanMeters <= LocalCompositionReferenceHorizontalSpanMeters)
    {
        return NormalGameplayAboveWaterFraction;
    }
    if (horizontalSpanMeters < TacticalCompositionHorizontalSpanMeters)
    {
        return smoothTransition(
            horizontalSpanMeters,
            LocalCompositionReferenceHorizontalSpanMeters,
            TacticalCompositionHorizontalSpanMeters,
            NormalGameplayAboveWaterFraction,
            TacticalGameplayAboveWaterFraction);
    }
    if (horizontalSpanMeters < OperationalCompositionHorizontalSpanMeters)
    {
        return smoothTransition(
            horizontalSpanMeters,
            TacticalCompositionHorizontalSpanMeters,
            OperationalCompositionHorizontalSpanMeters,
            TacticalGameplayAboveWaterFraction,
            OperationalGameplayAboveWaterFraction);
    }
    if (horizontalSpanMeters < StrategicCompositionHorizontalSpanMeters)
    {
        return smoothTransition(
            horizontalSpanMeters,
            OperationalCompositionHorizontalSpanMeters,
            StrategicCompositionHorizontalSpanMeters,
            OperationalGameplayAboveWaterFraction,
            StrategicGameplayAboveWaterFraction);
    }
    return StrategicGameplayAboveWaterFraction;
}

static_assert(NormalGameplayBandBottomReferenceYMeters == -700.0F);
static_assert(NormalGameplayPeriscopeZoneMaximumDepthMeters < NormalGameplayShallowZoneMaximumDepthMeters);
static_assert(NormalGameplayShallowZoneMaximumDepthMeters < NormalGameplayPrimaryCombatZoneMaximumDepthMeters);
static_assert(NormalGameplayPrimaryCombatZoneMaximumDepthMeters < NormalGameplayDeepTacticalZoneMaximumDepthMeters);
static_assert(NormalGameplayDeepTacticalZoneMaximumDepthMeters < NormalGameplayExtremeZoneMaximumDepthMeters);
static_assert(NormalGameplayExtremeZoneMaximumDepthMeters < NormalGameplayLowerBoundaryMaximumDepthMeters);
static_assert(NormalGameplayAboveWaterFraction >= 0.10F && NormalGameplayAboveWaterFraction <= 0.20F);
static_assert(TacticalGameplayAboveWaterFraction >= 0.25F && TacticalGameplayAboveWaterFraction <= 0.40F);
} // namespace DeepRun::Game
