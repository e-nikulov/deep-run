#pragma once

#include "Engine/Physics/PhysicsTypes.h"
#include "Engine/Render/Camera.h"
#include "Game/Camera/MultiScaleTacticalCamera.h"
#include "Game/Combat/PlayerCombatCommandRuntime.h"

#include <cstdint>
#include <optional>

namespace DeepRun::Game::Combat
{
// J2-B shipping-playground presentation only. The UI receives a read-only perceived-world/weapon projection;
// it cannot mutate TrackManager, weapon state, PhysicsWorld, or issue commands directly.
void DrawCombatCommandUi(const PlayerCombatPresentationSnapshot& snapshot);
void DrawSonarScope(const SonarPresentationSnapshot& snapshot);

struct CameraScaleHudSnapshot final
{
    Camera::MultiScaleCameraBand band = Camera::MultiScaleCameraBand::Local;
    float horizontalSpanMeters = Camera::MultiScaleInitialHorizontalSpanMeters;
    float maximumHorizontalSpanMeters = Camera::MultiScaleMaximumHorizontalSpanMeters;
    float ownshipProjectedPixels = 0.0F;
};

void DrawCameraScaleHud(const CameraScaleHudSnapshot& snapshot);

struct VesselNavigationHudSnapshot final
{
    float signedDepthMeters = 0.0F;
    float verticalSpeedMetersPerSecond = 0.0F;
    float throttleFraction = 0.0F;
    float bowPlaneDeflectionFraction = 0.0F;
    float sternPlaneDeflectionFraction = 0.0F;
};

void DrawVesselNavigationHud(const VesselNavigationHudSnapshot& snapshot);

void DrawTacticalSituationOverlay(
    Camera::MultiScaleCameraBand band,
    const Render::OrthographicCamera& camera,
    const Physics::PhysicsVector3& ownshipPositionMeters,
    const std::optional<Physics::PhysicsVector3>& selectedTrackEstimatedPositionMeters,
    const std::optional<std::uint64_t>& selectedTrackId,
    const std::optional<Physics::PhysicsVector3>& playerTorpedoPositionMeters);
} // namespace DeepRun::Game::Combat
