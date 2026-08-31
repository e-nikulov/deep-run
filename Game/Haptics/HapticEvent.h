#pragma once

namespace DeepRun::Game
{
enum class HapticEventType
{
    EngineVibration,
};

// Game-owned semantic feedback. Intensity is finite in [0, 1]; each event type defines its meaning.
struct HapticEvent final
{
    HapticEventType type = HapticEventType::EngineVibration;
    float intensity = 0.0F;
};
}
