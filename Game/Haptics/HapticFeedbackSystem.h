#pragma once

#include "Engine/Input/HapticMixer.h"
#include "Game/Haptics/HapticEvent.h"
#include "Simulation/Marine/PropulsionComponent.h"
#include "Simulation/Marine/PropulsionSystem.h"

#include <expected>
#include <string>

namespace DeepRun::Game
{
// Game-owned semantic producer/mapping. The constants below are M2 prototype feel tuning, not Engine
// policy and not a physical frequency model.
class HapticFeedbackSystem final
{
public:
    static constexpr Input::HapticEffectId EngineVibrationEffectId = 0x0001'0001U;

    [[nodiscard]] std::expected<HapticEvent, std::string> EngineVibrationFromShaftRpm(
        const Marine::PropulsionComponent& component,
        const Marine::PropulsionState& state) const;

    [[nodiscard]] std::expected<Input::HapticEffectRequest, std::string> Map(
        const HapticEvent& event) const;
};
}
