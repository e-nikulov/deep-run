#pragma once

#include "Engine/Input/GamepadVibration.h"

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace DeepRun::Input
{
using HapticEffectId = std::uint32_t;
inline constexpr HapticEffectId InvalidHapticEffectId = 0;

// Generic effect data only. Semantic meaning and tuning belong to the Game layer.
struct HapticEffectRequest final
{
    HapticEffectId id = InvalidHapticEffectId;
    float lowFrequencyMotor = 0.0F;
    float highFrequencyMotor = 0.0F;
    float durationSeconds = 0.0F;
    int priority = 0;
};

// Presentation-time mixer. Same-ID submissions replace and refresh an effect. Only the highest active
// priority participates; effects at that priority add and clamp before master intensity is applied.
class HapticMixer final
{
public:
    [[nodiscard]] std::expected<void, std::string> Submit(const HapticEffectRequest& request);
    [[nodiscard]] std::expected<void, std::string> Advance(float frameDeltaSeconds);
    [[nodiscard]] GamepadVibration CurrentOutput() const noexcept;

    void SetEnabled(bool enabled) noexcept;
    [[nodiscard]] bool Enabled() const noexcept;
    [[nodiscard]] std::expected<void, std::string> SetMasterIntensity(float intensity);
    [[nodiscard]] float MasterIntensity() const noexcept;

    // Clears active effects while preserving the user's enable/master settings.
    void Reset() noexcept;

private:
    struct ActiveEffect final
    {
        HapticEffectRequest request{};
        double remainingSeconds = 0.0;
    };

    std::vector<ActiveEffect> activeEffects_;
    float masterIntensity_ = 1.0F;
    bool enabled_ = true;
};
}
