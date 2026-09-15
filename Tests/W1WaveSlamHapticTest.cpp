#include "Game/Haptics/HapticFeedbackSystem.h"

#include <cmath>
#include <iostream>

namespace
{
bool NearlyEqual(const float a, const float b, const float tolerance = 1.0e-6F) noexcept
{
    return std::abs(a - b) <= tolerance;
}
}

int main()
{
    using namespace DeepRun;

    const Game::HapticFeedbackSystem feedback;
    const auto slam = feedback.Map(Game::HapticEvent{
        .type = Game::HapticEventType::WaveSlam,
        .intensity = 0.80F});
    if (!slam || slam->id != Game::HapticFeedbackSystem::WaveSlamEffectId ||
        !NearlyEqual(slam->lowFrequencyMotor, 0.76F) ||
        !NearlyEqual(slam->highFrequencyMotor, 0.28F) ||
        !NearlyEqual(slam->durationSeconds, 0.18F) || slam->priority != 40)
    {
        std::cerr << "W1-D wave-slam haptic mapping failed\n";
        return 1;
    }

    // Existing propulsion feedback remains a distinct, lower-priority continuous semantic effect.
    const auto engine = feedback.Map(Game::HapticEvent{
        .type = Game::HapticEventType::EngineVibration,
        .intensity = 0.80F});
    if (!engine || engine->id != Game::HapticFeedbackSystem::EngineVibrationEffectId ||
        engine->id == slam->id || engine->priority >= slam->priority ||
        !NearlyEqual(engine->lowFrequencyMotor, 0.44F) ||
        !NearlyEqual(engine->highFrequencyMotor, 0.08F) ||
        !NearlyEqual(engine->durationSeconds, 0.10F))
    {
        std::cerr << "W1-D changed the accepted engine-vibration haptic contract\n";
        return 1;
    }

    const auto silentSlam = feedback.Map(Game::HapticEvent{
        .type = Game::HapticEventType::WaveSlam,
        .intensity = 0.0F});
    if (!silentSlam || silentSlam->lowFrequencyMotor != 0.0F || silentSlam->highFrequencyMotor != 0.0F)
    {
        std::cerr << "zero-severity wave slam is not silent\n";
        return 1;
    }

    const auto malformed = feedback.Map(Game::HapticEvent{
        .type = Game::HapticEventType::WaveSlam,
        .intensity = 1.01F});
    if (malformed)
    {
        std::cerr << "out-of-range wave-slam severity was accepted\n";
        return 1;
    }

    std::cout << "W1-D semantic wave-slam haptics: PASS\n";
    return 0;
}
