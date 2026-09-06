#pragma once

#include <array>

namespace DeepRun::Marine
{
struct WaterWaveComponent final
{
    float amplitudeMeters = 0.0F;
    float wavelengthMeters = 0.0F;
    float angularFrequencyRadiansPerSecond = 0.0F;
    float phaseOffsetRadians = 0.0F;
    float horizontalSteepness = 0.0F;
};

// Marine owns the canonical definition; consumers copy values rather than owning independent tuning.
struct WaterWaveFieldDefinition final
{
    std::array<WaterWaveComponent, 3> components{};
};

inline constexpr WaterWaveFieldDefinition M3WaterWaveField{{{
    {1.75F, 100.0F, 0.28F, 0.20F, 0.55F},
    {0.80F, 45.0F, 0.48F, 1.40F, 0.40F},
    {0.35F, 20.0F, 0.82F, 2.30F, 0.20F}}}};
}
