#pragma once

#include "Game/Combat/PlayerCombatCommandRuntime.h"

namespace DeepRun::Game::Combat
{
// J2-B shipping-playground presentation only. The UI receives a read-only perceived-world/weapon projection;
// it cannot mutate TrackManager, weapon state, PhysicsWorld, or issue commands directly.
void DrawCombatCommandUi(const PlayerCombatPresentationSnapshot& snapshot);
} // namespace DeepRun::Game::Combat
