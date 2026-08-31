#include "Game/Haptics/HapticFeedbackSystem.h"

#include <cmath>

namespace DeepRun::Game
{
std::expected<HapticEvent, std::string> HapticFeedbackSystem::EngineVibrationFromShaftRpm(
    const Marine::PropulsionComponent& component,
    const Marine::PropulsionState& state) const
{
    if (!std::isfinite(component.maxForwardRpm) || component.maxForwardRpm <= 0.0F ||
        !std::isfinite(component.maxReverseRpm) || component.maxReverseRpm <= 0.0F)
    {
        return std::unexpected("engine-vibration RPM limits must be finite and greater than zero");
    }
    if (!std::isfinite(state.shaftRpm) || state.shaftRpm < -component.maxReverseRpm ||
        state.shaftRpm > component.maxForwardRpm)
    {
        return std::unexpected("engine-vibration shaft RPM is outside configured capability");
    }

    const double rpm = static_cast<double>(state.shaftRpm);
    const double normalizedRpm = rpm >= 0.0
                                     ? rpm / static_cast<double>(component.maxForwardRpm)
                                     : -rpm / static_cast<double>(component.maxReverseRpm);
    if (!std::isfinite(normalizedRpm) || normalizedRpm < 0.0 || normalizedRpm > 1.0)
    {
        return std::unexpected("engine-vibration normalized RPM must be finite and within [0, 1]");
    }
    return HapticEvent{
        .type = HapticEventType::EngineVibration,
        .intensity = normalizedRpm == 0.0 ? 0.0F : static_cast<float>(normalizedRpm)};
}

std::expected<Input::HapticEffectRequest, std::string> HapticFeedbackSystem::Map(
    const HapticEvent& event) const
{
    if (!std::isfinite(event.intensity) || event.intensity < 0.0F || event.intensity > 1.0F)
    {
        return std::unexpected("semantic haptic intensity must be finite and within [0, 1]");
    }

    const float intensity = event.intensity == 0.0F ? 0.0F : event.intensity;
    switch (event.type)
    {
    case HapticEventType::EngineVibration:
        return Input::HapticEffectRequest{
            .id = EngineVibrationEffectId,
            .lowFrequencyMotor = 0.55F * intensity,
            .highFrequencyMotor = 0.10F * intensity,
            .durationSeconds = 0.10F,
            .priority = 10};
    }
    return std::unexpected("unknown semantic haptic event type");
}
}
