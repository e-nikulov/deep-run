#include "Game/Submarine/VesselCommandState.h"

#include "Engine/Input/InputState.h"

#include <cmath>

namespace DeepRun::Game
{
std::expected<void, std::string> ValidateVesselCommandState(const VesselCommandState& command)
{
    const auto validFraction = [](const float value) {
        return std::isfinite(value) && value >= -1.0F && value <= 1.0F;
    };
    if (!validFraction(command.throttleFraction))
    {
        return std::unexpected("vessel throttle command must be finite and within [-1, +1]");
    }
    if (!validFraction(command.depthCommandFraction))
    {
        return std::unexpected("vessel depth command must be finite and within [-1, +1]");
    }
    return {};
}

std::expected<VesselCommandState, std::string> VesselCommandStateFromInput(const Input::InputState& input)
{
    const VesselCommandState command{
        .throttleFraction = input.Axis(Input::InputAxis::Throttle),
        .depthCommandFraction = input.Axis(Input::InputAxis::Depth)};
    const auto validated = ValidateVesselCommandState(command);
    if (!validated)
    {
        return std::unexpected(validated.error());
    }
    return command;
}
}
