#pragma once

#include "Game/Camera/MultiScaleTacticalCamera.h"
#include "Game/Combat/PlayerCombatCommandRuntime.h"

#include <cstdint>

namespace DeepRun::Game::Combat
{
// J2-B shipping-playground presentation only. The UI receives a read-only perceived-world/weapon projection;
// it cannot mutate TrackManager, weapon state, PhysicsWorld, or issue commands directly.
void DrawCombatCommandUi(const PlayerCombatPresentationSnapshot& snapshot);

struct CameraScaleHudSnapshot final
{
    Camera::MultiScaleCameraBand band = Camera::MultiScaleCameraBand::Local;
    float horizontalSpanMeters = Camera::MultiScaleInitialHorizontalSpanMeters;
    float maximumHorizontalSpanMeters = Camera::MultiScaleMaximumHorizontalSpanMeters;
    float ownshipProjectedPixels = 0.0F;
};

void DrawCameraScaleHud(const CameraScaleHudSnapshot& snapshot);
} // namespace DeepRun::Game::Combat
