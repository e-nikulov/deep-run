#include "Engine/Input/HapticMixer.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace DeepRun::Input
{
namespace
{
bool IsNormalized(const float value) noexcept
{
    return std::isfinite(value) && value >= 0.0F && value <= 1.0F;
}
}

std::expected<void, std::string> HapticMixer::Submit(const HapticEffectRequest& request)
{
    if (request.id == InvalidHapticEffectId)
    {
        return std::unexpected("haptic effect ID must not be the reserved invalid value");
    }
    if (!IsNormalized(request.lowFrequencyMotor) || !IsNormalized(request.highFrequencyMotor))
    {
        return std::unexpected("haptic motor magnitudes must be finite and within [0, 1]");
    }
    if (!std::isfinite(request.durationSeconds) || request.durationSeconds <= 0.0F)
    {
        return std::unexpected("haptic effect duration must be finite and greater than zero");
    }

    const auto existing = std::ranges::find_if(activeEffects_, [&request](const ActiveEffect& effect) {
        return effect.request.id == request.id;
    });
    const ActiveEffect refreshed{
        .request = request,
        .remainingSeconds = static_cast<double>(request.durationSeconds)};
    if (existing != activeEffects_.end())
    {
        *existing = refreshed;
    }
    else
    {
        activeEffects_.push_back(refreshed);
    }
    return {};
}

std::expected<void, std::string> HapticMixer::Advance(const float frameDeltaSeconds)
{
    if (!std::isfinite(frameDeltaSeconds) || frameDeltaSeconds < 0.0F)
    {
        return std::unexpected("haptic frame delta must be finite and non-negative");
    }

    const double deltaSeconds = static_cast<double>(frameDeltaSeconds);
    for (ActiveEffect& effect : activeEffects_)
    {
        effect.remainingSeconds -= deltaSeconds;
    }
    std::erase_if(activeEffects_, [](const ActiveEffect& effect) {
        return !std::isfinite(effect.remainingSeconds) || effect.remainingSeconds <= 0.0;
    });
    return {};
}

GamepadVibration HapticMixer::CurrentOutput() const noexcept
{
    if (!enabled_ || activeEffects_.empty() || masterIntensity_ == 0.0F)
    {
        return {};
    }

    int winningPriority = (std::numeric_limits<int>::min)();
    for (const ActiveEffect& effect : activeEffects_)
    {
        winningPriority = (std::max)(winningPriority, effect.request.priority);
    }

    double low = 0.0;
    double high = 0.0;
    for (const ActiveEffect& effect : activeEffects_)
    {
        if (effect.request.priority == winningPriority)
        {
            low += static_cast<double>(effect.request.lowFrequencyMotor);
            high += static_cast<double>(effect.request.highFrequencyMotor);
        }
    }

    const double master = static_cast<double>(masterIntensity_);
    return {
        .lowFrequencyMotor = static_cast<float>((std::min)(low, 1.0) * master),
        .highFrequencyMotor = static_cast<float>((std::min)(high, 1.0) * master)};
}

void HapticMixer::SetEnabled(const bool enabled) noexcept
{
    enabled_ = enabled;
}

bool HapticMixer::Enabled() const noexcept
{
    return enabled_;
}

std::expected<void, std::string> HapticMixer::SetMasterIntensity(const float intensity)
{
    if (!IsNormalized(intensity))
    {
        return std::unexpected("haptic master intensity must be finite and within [0, 1]");
    }
    masterIntensity_ = intensity;
    return {};
}

float HapticMixer::MasterIntensity() const noexcept
{
    return masterIntensity_;
}

void HapticMixer::Reset() noexcept
{
    activeEffects_.clear();
}
}
