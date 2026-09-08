#pragma once

#include "Game/Submarine/AnteyAcousticModel.h"

namespace DeepRun::Tests
{
[[nodiscard]] inline bool RunM4CavitationChecks()
{
    const Acoustics::AcousticSpectrum ambient{.levelDb = {40.0F, 40.0F, 40.0F, 40.0F}};

    Game::Submarine::AnteyAcousticRuntimeState quiet{};
    quiet.shaftRpm = 45.0F;
    quiet.linearVelocityMetersPerSecond = {3.0F, 0.0F, 0.0F};
    quiet.signedDepthMeters = 100.0F;

    Game::Submarine::AnteyAcousticRuntimeState shallowFast{};
    shallowFast.shaftRpm = 180.0F;
    shallowFast.linearVelocityMetersPerSecond = {16.0F, 0.0F, 0.0F};
    shallowFast.signedDepthMeters = 30.0F;

    Game::Submarine::AnteyAcousticRuntimeState deepFast = shallowFast;
    deepFast.signedDepthMeters = 300.0F;

    const auto quietSnapshot = Game::Submarine::BuildAnteyAcousticSnapshot(quiet, ambient);
    const auto shallowSnapshot = Game::Submarine::BuildAnteyAcousticSnapshot(shallowFast, ambient);
    const auto deepSnapshot = Game::Submarine::BuildAnteyAcousticSnapshot(deepFast, ambient);
    if (!quietSnapshot || !shallowSnapshot || !deepSnapshot)
    {
        return false;
    }

    if (quietSnapshot->cavitationIntensity != 0.0F ||
        !(shallowSnapshot->cavitationIntensity > deepSnapshot->cavitationIntensity) ||
        !(deepSnapshot->cavitationIntensity > 0.0F))
    {
        return false;
    }

    // Cavitation is broadband but deliberately weighted toward higher bands. The same fast state deeper down
    // must emit less cavitation-driven energy and create a lower own passive noise floor than shallow operation.
    for (std::size_t band = 0; band < Acoustics::AcousticBandCount; ++band)
    {
        if (!(shallowSnapshot->emitter.continuousSourceLevelDb.levelDb[band] >
              deepSnapshot->emitter.continuousSourceLevelDb.levelDb[band]) ||
            !(shallowSnapshot->passiveReceiver.selfNoiseLevelDb.levelDb[band] >
              deepSnapshot->passiveReceiver.selfNoiseLevelDb.levelDb[band]))
        {
            return false;
        }
    }

    if (!(shallowSnapshot->emitter.continuousSourceLevelDb[Acoustics::AcousticBand::High] -
          deepSnapshot->emitter.continuousSourceLevelDb[Acoustics::AcousticBand::High] >
          shallowSnapshot->emitter.continuousSourceLevelDb[Acoustics::AcousticBand::VeryLow] -
          deepSnapshot->emitter.continuousSourceLevelDb[Acoustics::AcousticBand::VeryLow]))
    {
        return false;
    }

    Game::Submarine::AnteyAcousticRuntimeState invalid{};
    invalid.signedDepthMeters = std::nanf("");
    return !Game::Submarine::BuildAnteyAcousticSnapshot(invalid, ambient);
}
}
