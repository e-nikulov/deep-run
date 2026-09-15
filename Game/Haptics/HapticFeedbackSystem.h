#pragma once

#include "Engine/Input/HapticMixer.h"
#include "Game/Haptics/HapticEvent.h"
#include "Simulation/Marine/PropulsionComponent.h"
#include "Simulation/Marine/PropulsionSystem.h"

#include <expected>
#include <string>

namespace DeepRun::Game
{
// Game-owned semantic producer/mapping. The constants below are presentation feel tuning, not Engine policy
// and not a structural-load model. Marine supplies dimensional impact evidence; Game maps normalized severity.
class HapticFeedbackSystem final
{
public:
    static constexpr Input::HapticEffectId EngineVibrationEffectId = 0x0001'0001U;
    static constexpr Input::HapticEffectId WaveSlamEffectId = 0x0001'0002U;

    [[nodiscard]] std::expected<HapticEvent, std::string> EngineVibrationFromShaftRpm(
        const Marine::PropulsionComponent& component,
        const Marine::PropulsionState& state) const;

    [[nodiscard]] std::expected<Input::HapticEffectRequest, std::string> Map(
        const HapticEvent& event) const;
};
}
