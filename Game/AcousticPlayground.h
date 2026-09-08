#pragma once

#include "Game/Submarine/AnteyAcousticModel.h"
#include "Simulation/Acoustics/AcousticWorld.h"

#include <expected>
#include <optional>
#include <string>
#include <utility>

namespace DeepRun::Game
{
// First bounded M4 end-to-end playground. It intentionally uses one synthetic external transient so the
// acoustic/perception path can be proven before M5 introduces combat targets. No source identity is published.
class AcousticPlayground final
{
public:
    [[nodiscard]] static std::expected<AcousticPlayground, std::string> Create()
    {
        const auto world = Acoustics::AcousticWorld::Create({});
        if (!world)
        {
            return std::unexpected(world.error().message);
        }
        return AcousticPlayground(*world);
    }

    [[nodiscard]] std::expected<std::optional<Acoustics::AcousticObservation>, std::string>
    CollectSyntheticPassiveObservation(
        const Submarine::AnteyAcousticRuntimeState& playerState,
        const Acoustics::AcousticSpectrum& ambientNoiseLevelDb,
        const double simulationTimeSeconds) const
    {
        const auto antey = Submarine::BuildAnteyAcousticSnapshot(playerState, ambientNoiseLevelDb);
        if (!antey)
        {
            return std::unexpected(antey.error());
        }

        // Game-owned engineering source used only to prove the M4 perception path. Values are deliberately
        // synthetic gameplay tuning, not a real vessel signature or an authoritative target entity.
        constexpr Acoustics::AcousticEmission syntheticTransient{
            .positionMeters = {4500.0F, -100.0F, 0.0F},
            .sourceLevelDb = {.levelDb = {178.0F, 172.0F, 164.0F, 154.0F}},
            .emissionTimeSeconds = 0.5};

        const auto observation = world_.CollectPassiveDirectObservation(
            syntheticTransient, antey->passiveReceiver, simulationTimeSeconds);
        if (!observation)
        {
            return std::unexpected(observation.error().message);
        }
        return *observation;
    }

private:
    explicit AcousticPlayground(Acoustics::AcousticWorld world)
        : world_(std::move(world))
    {
    }

    Acoustics::AcousticWorld world_;
};
}
