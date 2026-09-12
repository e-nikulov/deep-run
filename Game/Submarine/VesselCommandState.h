#pragma once

#include <cstdint>
#include <expected>
#include <string>

namespace DeepRun::Input
{
class InputState;
}

namespace DeepRun::Game
{
// Authoritative direct-vessel command snapshot for one fixed simulation tick. It deliberately carries no
// device state, physics state, RPM, force, or presentation data.
struct VesselCommandState final
{
    // [-1, +1]: astern -> ahead.
    float throttleFraction = 0.0F;
    // [-1, +1]: surface / nose-up -> dive / nose-down.
    float depthCommandFraction = 0.0F;
    // Monotonic semantic edge; PhysicalPlayground consumes each requested 180-degree 2.5D turn once.
    std::uint64_t turnAroundPressSequence = 0;
};

[[nodiscard]] std::expected<VesselCommandState, std::string> VesselCommandStateFromInput(
    const Input::InputState& input);
[[nodiscard]] std::expected<void, std::string> ValidateVesselCommandState(const VesselCommandState& command);
}
