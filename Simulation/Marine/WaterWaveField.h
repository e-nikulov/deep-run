#pragma once

#include <array>
#include <cstddef>

namespace DeepRun::Marine
{
inline constexpr std::size_t WaterWaveComponentCapacity = 7U;

struct WaterWaveComponent final
{
    float amplitudeMeters = 0.0F;
    float wavelengthMeters = 0.0F;
    // Signed angular frequency. Magnitude is radians/second; sign encodes the projected +X/-X travel direction
    // for Deep Run's 2.5D production surface. Zero is never valid for an active component.
    float angularFrequencyRadiansPerSecond = 0.0F;
    float phaseOffsetRadians = 0.0F;
    float horizontalSteepness = 0.0F;
};

// Marine owns the canonical bounded wave definition. Seven components are the W1-B production budget:
// enough to separate swell / wind-sea / short-wave energy while still fitting one D3D12 draw's 64-DWORD
// root-signature limit without descriptors or per-frame allocations.
struct WaterWaveFieldDefinition final
{
    std::array<WaterWaveComponent, WaterWaveComponentCapacity> components{};
};

// Legacy M3 acceptance field retained only for old tests/reference scenarios. Production gameplay builds its
// seven-component spectrum from W1 WeatherState instead of selecting this hand-authored field.
inline constexpr WaterWaveFieldDefinition M3WaterWaveField{{{
    {1.75F, 100.0F, 0.28F, 0.20F, 0.55F},
    {0.80F, 45.0F, 0.48F, 1.40F, 0.40F},
    {0.35F, 20.0F, 0.82F, 2.30F, 0.20F},
    {0.12F, 13.0F, 1.05F, 0.75F, 0.14F},
    {0.08F, 9.0F, 1.28F, 3.10F, 0.10F},
    {0.05F, 6.5F, 1.52F, 4.35F, 0.08F},
    {0.03F, 4.5F, 1.82F, 5.20F, 0.06F}}}};
}
