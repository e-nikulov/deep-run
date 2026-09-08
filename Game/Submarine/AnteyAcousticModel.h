#pragma once

#include "Engine/Physics/PhysicsTypes.h"
#include "Simulation/Acoustics/AcousticTypes.h"

#include <expected>
#include <string>
#include <string_view>

namespace DeepRun::Game::Submarine
{
// Canonical production semantic region from Antey.authoring.json. The content contract explicitly states that
// this region has NO_GEOMETRIC_ANCHOR_AUTHORED, so M4-A.1 must not invent a fake bow-array transform.
inline constexpr std::string_view AnteyMainPassiveArraySensorId = "MGK540_BOW_ARRAY";

// Plain authoritative runtime inputs. This composition layer knows nothing about Jolt handles, GLB nodes,
// renderer state, input devices, AudioEngine/miniaudio, or authoring hierarchy.
struct AnteyAcousticRuntimeState final
{
    Physics::PhysicsVector3 bodyReferencePositionMeters{};
    Physics::PhysicsVector3 linearVelocityMetersPerSecond{};
    float shaftRpm = 0.0F;
};

struct AnteyAcousticSnapshot final
{
    Acoustics::AcousticEmitter emitter{};
    Acoustics::AcousticReceiver passiveReceiver{};
};

// Builds the current coarse gameplay signature and passive receiver state for the production Antey runtime.
// The values are deliberately authored gameplay tuning, not measured/classified Project 949A acoustic data.
// Until a geometric sonar anchor is authored, receiver position is the authoritative body reference point;
// the semantic sensor identity remains MGK540_BOW_ARRAY without claiming nonexistent antenna geometry.
[[nodiscard]] std::expected<AnteyAcousticSnapshot, std::string> BuildAnteyAcousticSnapshot(
    const AnteyAcousticRuntimeState& state,
    const Acoustics::AcousticSpectrum& ambientNoiseLevelDb);
}
