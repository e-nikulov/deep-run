#pragma once

namespace DeepRun::Marine
{
// Pure tuning for one independently simulated shaft/propulsor (M2 Slice G1). All maxima and response
// rates are positive magnitudes; signed direction comes only from the command and authoritative shaft RPM.
// Vessel mass, force application, water, drag, cavitation, and presentation are separate concerns.
struct PropulsionComponent final
{
    float maxForwardRpm = 0.0F;
    float maxReverseRpm = 0.0F;

    float maxForwardThrustNewtons = 0.0F;
    float maxReverseThrustNewtons = 0.0F;

    float spinUpRateRpmPerSecond = 0.0F;
    float spinDownRateRpmPerSecond = 0.0F;
};
}
