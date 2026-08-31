#pragma once

namespace DeepRun::Input
{
// Generic presentation output. Platform backends convert these normalized motor magnitudes to their
// native representation; gameplay never observes or supplies a platform-specific motor type.
struct GamepadVibration final
{
    float lowFrequencyMotor = 0.0F;
    float highFrequencyMotor = 0.0F;

    friend bool operator==(const GamepadVibration&, const GamepadVibration&) = default;
};
}
