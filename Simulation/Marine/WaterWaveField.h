#pragma once

#include <array>
#include <cstddef>

namespace DeepRun::Marine
{
inline constexpr std::size_t WaterWaveComponentCapacity = 7U;
inline constexpr std::size_t LegacyM3WaterWaveComponentCount = 3U;

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

// Marine owns the canonical bounded wave definition. Capacity is seven for W1-B, but activeComponentCount
// preserves the accepted M3 three-wave contract for legacy/reference scenarios. Inactive tail entries have no
// authority and are never sampled or copied to presentation.
struct WaterWaveFieldDefinition final
{
    std::array<WaterWaveComponent, WaterWaveComponentCapacity> components{};
    std::size_t activeComponentCount = LegacyM3WaterWaveComponentCount;
};

// Exact accepted M3 field: three active components. Production gameplay constructs seven active components
// from W1 WeatherState instead of mutating this legacy/reference contract.
inline constexpr WaterWaveFieldDefinition M3WaterWaveField{
    .components = {{
        {1.75F, 100.0F, 0.28F, 0.20F, 0.55F},
        {0.80F, 45.0F, 0.48F, 1.40F, 0.40F},
        {0.35F, 20.0F, 0.82F, 2.30F, 0.20F}}},
    .activeComponentCount = LegacyM3WaterWaveComponentCount};
}
