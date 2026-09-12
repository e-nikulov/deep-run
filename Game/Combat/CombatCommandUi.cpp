#include "Game/Combat/CombatCommandUi.h"
#include "Game/Weapons/P700LauncherInventory.h"

#include <imgui.h>

#include <cmath>
#include <optional>
#include <string_view>

namespace DeepRun::Game::Combat
{
namespace
{
const char* WeaponPhaseName(const Weapons::WeaponPhase phase) noexcept
{
    switch (phase)
    {
    case Weapons::WeaponPhase::Stored: return "STORED";
    case Weapons::WeaponPhase::Preparing: return "PREPARING";
    case Weapons::WeaponPhase::Ready: return "READY";
    case Weapons::WeaponPhase::Launched: return "LAUNCHED";
    }
    return "UNKNOWN";
}

const char* TrackLifecycleName(const Perception::TrackLifecycleState lifecycle) noexcept
{
    switch (lifecycle)
    {
    case Perception::TrackLifecycleState::Tentative: return "TENTATIVE";
    case Perception::TrackLifecycleState::Confirmed: return "CONFIRMED";
    case Perception::TrackLifecycleState::Coasting: return "COASTING";
    case Perception::TrackLifecycleState::Lost: return "LOST";
    }
    return "UNKNOWN";
}

const char* CameraBandName(const Camera::MultiScaleCameraBand band) noexcept
{
    switch (band)
    {
    case Camera::MultiScaleCameraBand::Detail: return "DETAIL";
    case Camera::MultiScaleCameraBand::Local: return "LOCAL";
    case Camera::MultiScaleCameraBand::Tactical: return "TACTICAL";
    case Camera::MultiScaleCameraBand::Operational: return "OPERATIONAL";
    case Camera::MultiScaleCameraBand::Strategic: return "STRATEGIC";
    }
    return "UNKNOWN";
}

std::optional<ImVec2> ProjectWorldToMainViewport(
    const Render::OrthographicCamera& camera,
    const Physics::PhysicsVector3& positionMeters)
{
    if (!positionMeters.IsFinite())
    {
        return std::nullopt;
    }
    const auto clip = Render::TransformPoint(
        camera.viewProjection,
        {.x = positionMeters.x, .y = positionMeters.y, .z = positionMeters.z});
    if (!std::isfinite(clip[0]) || !std::isfinite(clip[1]) || !std::isfinite(clip[3]) ||
        std::abs(clip[3]) <= 1.0e-6F)
    {
        return std::nullopt;
    }
    const float ndcX = clip[0] / clip[3];
    const float ndcY = clip[1] / clip[3];
    if (ndcX < -1.05F || ndcX > 1.05F || ndcY < -1.05F || ndcY > 1.05F)
    {
        return std::nullopt;
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (viewport == nullptr)
    {
        return std::nullopt;
    }
    return ImVec2(
        viewport->WorkPos.x + (ndcX * 0.5F + 0.5F) * viewport->WorkSize.x,
        viewport->WorkPos.y + (0.5F - ndcY * 0.5F) * viewport->WorkSize.y);
}

const char* CommandName(const PlayerCombatCommandType command) noexcept
{
    switch (command)
    {
    case PlayerCombatCommandType::SelectNextTrack: return "SELECT CONTACT";
    case PlayerCombatCommandType::PreviousWeapon: return "PREVIOUS WEAPON";
    case PlayerCombatCommandType::NextWeapon: return "NEXT WEAPON";
    case PlayerCombatCommandType::PrepareWeapon: return "PREPARE WEAPON";
    case PlayerCombatCommandType::FireWeapon: return "FIRE WEAPON";
    case PlayerCombatCommandType::ActiveSonarPing: return "ACTIVE SONAR";
    case PlayerCombatCommandType::DeployDecoy: return "DEPLOY DECOY";
    }
    return "UNKNOWN COMMAND";
}
}

void DrawCombatCommandUi(
    const PlayerCombatPresentationSnapshot& snapshot,
    CombatUiPresentationSettings* presentationSettings)
{
    // M5-V1: the Engine diagnostics overlay owns the upper-left corner. Combat presentation is anchored to
    // the upper-right work area on every frame so resize/capture cannot reintroduce the old overlap.
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (viewport != nullptr)
    {
        constexpr float margin = 16.0F;
        ImGui::SetNextWindowPos(
            ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - margin, viewport->WorkPos.y + margin),
            ImGuiCond_Always,
            ImVec2(1.0F, 0.0F));
    }
    ImGui::SetNextWindowSize(ImVec2(350.0F, 0.0F), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.82F);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize |
                                       ImGuiWindowFlags_NoCollapse |
                                       ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_NoNavInputs;
    if (!ImGui::Begin("COMBAT", nullptr, flags))
    {
        ImGui::End();
        return;
    }

    const std::string_view weaponName = Armament::PlayerWeaponName(snapshot.selectedWeapon);
    ImGui::Text("Weapon: %.*s / %s", static_cast<int>(weaponName.size()), weaponName.data(), WeaponPhaseName(snapshot.weaponPhase));
    ImGui::Text("P-700 loaded: %zu / %zu", snapshot.p700LoadedCount, Armament::AnteyP700LauncherSlotCount);
    if (snapshot.weaponTargetTrackId)
    {
        ImGui::Text("Weapon track: #%llu", static_cast<unsigned long long>(*snapshot.weaponTargetTrackId));
    }

    ImGui::Separator();
    if (!snapshot.selectedTrackId)
    {
        ImGui::TextUnformatted("Selected track: NONE");
        ImGui::TextUnformatted("Firing solution: NO TRACK");
    }
    else
    {
        ImGui::Text("Selected track: #%llu", static_cast<unsigned long long>(*snapshot.selectedTrackId));
        if (!snapshot.selectedTrackPresent)
        {
            ImGui::TextUnformatted("Track state: NOT PRESENT");
        }
        else
        {
            if (snapshot.selectedTrackLifecycle)
            {
                ImGui::Text("Track state: %s", TrackLifecycleName(*snapshot.selectedTrackLifecycle));
            }
            if (snapshot.selectedTrackConfidence)
            {
                ImGui::Text("Confidence: %.0f%%", *snapshot.selectedTrackConfidence * 100.0F);
            }
            if (snapshot.selectedBearingUncertaintyRadians)
            {
                constexpr float radiansToDegrees = 57.2957795F;
                ImGui::Text("Bearing uncertainty: +/- %.1f deg",
                            *snapshot.selectedBearingUncertaintyRadians * radiansToDegrees);
            }
            if (snapshot.selectedPositionUncertaintyMeters)
            {
                ImGui::Text("Position uncertainty: +/- %.0f m", *snapshot.selectedPositionUncertaintyMeters);
            }
            else
            {
                ImGui::TextUnformatted("Position solution: unavailable");
            }
            ImGui::Text("Firing solution: %s", snapshot.selectedTrackWeaponQualified ? "QUALIFIED" : "INSUFFICIENT");
        }
    }

    ImGui::Separator();
    ImGui::TextUnformatted("THREAT");
    if (!snapshot.incomingThreatDetected)
    {
        ImGui::TextUnformatted("Incoming acoustic threat: NONE");
    }
    else
    {
        ImGui::TextUnformatted("Incoming acoustic threat: DETECTED");
        if (snapshot.incomingThreatLifecycle)
        {
            ImGui::Text("Threat track: %s", TrackLifecycleName(*snapshot.incomingThreatLifecycle));
        }
        if (snapshot.incomingThreatBearingRadians)
        {
            constexpr float radiansToDegrees = 57.2957795F;
            ImGui::Text("Threat bearing: %.1f deg", *snapshot.incomingThreatBearingRadians * radiansToDegrees);
        }
        if (snapshot.incomingThreatBearingUncertaintyRadians)
        {
            constexpr float radiansToDegrees = 57.2957795F;
            ImGui::Text("Threat bearing uncertainty: +/- %.1f deg",
                        *snapshot.incomingThreatBearingUncertaintyRadians * radiansToDegrees);
        }
        if (snapshot.incomingThreatConfidence)
        {
            ImGui::Text("Threat confidence: %.0f%%", *snapshot.incomingThreatConfidence * 100.0F);
        }
    }

    ImGui::Separator();
    ImGui::Text("Prepare available: %s", snapshot.canPrepareWeapon ? "YES" : "NO");
    ImGui::Text("Fire available: %s", snapshot.canFireWeapon ? "YES" : "NO");
    ImGui::Text("Active sonar: %s",
                snapshot.activeSonarPulsePending ? "PING OUT" : (snapshot.canActiveSonarPing ? "READY" : "UNAVAILABLE"));
    if (presentationSettings != nullptr)
    {
        ImGui::Checkbox("Sonar visualization", &presentationSettings->sonarVisualizationEnabled);
    }
    ImGui::Text("Decoy available: %s", snapshot.canDeployDecoy ? "YES" : "NO");
    ImGui::Text("Player decoy active: %s", snapshot.playerDecoyActive ? "YES" : "NO");
    if (snapshot.lastCommand)
    {
        ImGui::Separator();
        ImGui::Text("Last: %s - %s",
                    CommandName(snapshot.lastCommand->command),
                    snapshot.lastCommand->accepted ? "ACCEPTED" : "REJECTED");
        ImGui::TextWrapped("%s", snapshot.lastCommand->message.c_str());
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Y / Tab          Select contact");
    ImGui::TextUnformatted("D-pad L/R / Z/C  Select weapon");
    ImGui::TextUnformatted("LT / R / RMB     Prepare weapon");
    ImGui::TextUnformatted("RT / LMB         Fire weapon");
    ImGui::TextUnformatted("RB / Space       Active sonar ping");
    ImGui::TextUnformatted("X / F            Deploy decoy");
    ImGui::End();
}


void DrawSonarScope(const SonarPresentationSnapshot& snapshot)
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (viewport != nullptr)
    {
        constexpr float margin = 16.0F;
        ImGui::SetNextWindowPos(
            ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - margin,
                   viewport->WorkPos.y + viewport->WorkSize.y - margin),
            ImGuiCond_Always,
            ImVec2(1.0F, 1.0F));
    }
    ImGui::SetNextWindowSize(ImVec2(330.0F, 370.0F), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.88F);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse |
                                       ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_NoNavInputs |
                                       ImGuiWindowFlags_NoInputs;
    if (!ImGui::Begin("SONAR", nullptr, flags))
    {
        ImGui::End();
        return;
    }

    ImGui::Text("Range: %.1f km", snapshot.displayRangeMeters / 1000.0F);
    ImGui::SameLine();
    ImGui::Text("Contacts: %d", static_cast<int>(snapshot.tracks.size()));
    if (snapshot.activePulse)
    {
        ImGui::SameLine();
        ImGui::TextUnformatted("PING OUT");
    }
    else if (snapshot.recentEcho)
    {
        ImGui::SameLine();
        ImGui::TextUnformatted("ECHO");
    }

    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float scopeSize = std::max(160.0F, std::min(available.x, available.y - 22.0F));
    const ImVec2 topLeft = ImGui::GetCursorScreenPos();
    const ImVec2 center(topLeft.x + scopeSize * 0.5F, topLeft.y + scopeSize * 0.5F);
    const float radius = scopeSize * 0.46F;
    ImGui::InvisibleButton("##SONAR_SCOPE", ImVec2(scopeSize, scopeSize));
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    if (drawList == nullptr || snapshot.displayRangeMeters <= 0.0F)
    {
        ImGui::End();
        return;
    }

    const ImU32 gridColor = IM_COL32(90, 135, 145, 120);
    const ImU32 textColor = IM_COL32(185, 225, 230, 220);
    const ImU32 contactColor = IM_COL32(105, 235, 190, 235);
    const ImU32 selectedColor = IM_COL32(255, 220, 100, 255);
    const ImU32 pingColor = IM_COL32(90, 210, 255, 210);
    const ImU32 echoColor = IM_COL32(255, 175, 90, 240);

    for (int ring = 1; ring <= 4; ++ring)
    {
        drawList->AddCircle(center, radius * (static_cast<float>(ring) / 4.0F), gridColor, 64, 1.0F);
    }
    drawList->AddLine(ImVec2(center.x - radius, center.y), ImVec2(center.x + radius, center.y), gridColor, 1.0F);
    drawList->AddLine(ImVec2(center.x, center.y - radius), ImVec2(center.x, center.y + radius), gridColor, 1.0F);
    drawList->AddTriangleFilled(
        ImVec2(center.x + radius + 2.0F, center.y),
        ImVec2(center.x + radius - 6.0F, center.y - 4.0F),
        ImVec2(center.x + radius - 6.0F, center.y + 4.0F),
        textColor);
    drawList->AddText(ImVec2(center.x + radius - 18.0F, center.y + 7.0F), textColor, "BOW");

    const auto pointAt = [&center](const float bearingRadians, const float distancePixels) {
        return ImVec2(
            center.x + std::cos(bearingRadians) * distancePixels,
            center.y - std::sin(bearingRadians) * distancePixels);
    };

    for (const auto& contact : snapshot.tracks)
    {
        const ImU32 color = contact.selected ? selectedColor : contactColor;
        const float bearing = contact.relativeBearingRadians;
        const float uncertainty = std::min(contact.bearingUncertaintyRadians, 1.2F);
        if (!contact.estimatedRangeMeters)
        {
            const ImVec2 bearingPoint = pointAt(bearing, radius);
            drawList->AddLine(center, bearingPoint, color, contact.selected ? 2.0F : 1.0F);
            drawList->AddLine(center, pointAt(bearing - uncertainty, radius), IM_COL32(105, 235, 190, 80), 1.0F);
            drawList->AddLine(center, pointAt(bearing + uncertainty, radius), IM_COL32(105, 235, 190, 80), 1.0F);
            drawList->AddCircleFilled(bearingPoint, contact.selected ? 5.0F : 3.5F, color, 12);
        }
        else
        {
            const float normalizedRange = std::clamp(*contact.estimatedRangeMeters / snapshot.displayRangeMeters, 0.0F, 1.0F);
            const ImVec2 contactPoint = pointAt(bearing, radius * normalizedRange);
            if (contact.positionUncertaintyMeters)
            {
                const float uncertaintyPixels = std::clamp(
                    *contact.positionUncertaintyMeters / snapshot.displayRangeMeters * radius, 2.0F, radius * 0.25F);
                drawList->AddCircle(contactPoint, uncertaintyPixels, IM_COL32(105, 235, 190, 100), 24, 1.0F);
            }
            drawList->AddCircleFilled(contactPoint, contact.selected ? 5.5F : 4.0F, color, 16);
            drawList->AddLine(
                pointAt(bearing - uncertainty, radius * normalizedRange),
                pointAt(bearing + uncertainty, radius * normalizedRange),
                IM_COL32(105, 235, 190, 100), 1.0F);
        }

        const ImVec2 labelPoint = pointAt(
            bearing,
            contact.estimatedRangeMeters
                ? radius * std::clamp(*contact.estimatedRangeMeters / snapshot.displayRangeMeters, 0.0F, 1.0F)
                : radius);
        const std::string label = "#" + std::to_string(static_cast<unsigned long long>(contact.trackId));
        drawList->AddText(ImVec2(labelPoint.x + 6.0F, labelPoint.y - 7.0F), color, label.c_str());
    }

    if (snapshot.activePulse)
    {
        const double elapsedSeconds = std::max(0.0, snapshot.simulationTimeSeconds - snapshot.activePulse->emissionTimeSeconds);
        constexpr double soundSpeedMetersPerSecond = 1500.0;
        const float waveRangeMeters = static_cast<float>(elapsedSeconds * soundSpeedMetersPerSecond);
        const float waveRadius = radius * std::clamp(waveRangeMeters / snapshot.displayRangeMeters, 0.0F, 1.0F);
        const float bearing = snapshot.activePulse->relativeBearingRadians;
        const float halfAngle = snapshot.activePulse->beamHalfAngleRadians;
        drawList->AddLine(center, pointAt(bearing - halfAngle, radius), pingColor, 1.0F);
        drawList->AddLine(center, pointAt(bearing + halfAngle, radius), pingColor, 1.0F);
        if (waveRadius > 1.0F)
        {
            drawList->PathClear();
            drawList->PathArcTo(center, waveRadius, -(bearing + halfAngle), -(bearing - halfAngle), 24);
            drawList->PathStroke(pingColor, 0, 2.0F);
        }
    }

    if (snapshot.recentEcho)
    {
        const float normalizedRange = std::clamp(
            snapshot.recentEcho->estimatedRangeMeters / snapshot.displayRangeMeters, 0.0F, 1.0F);
        const ImVec2 echoPoint = pointAt(snapshot.recentEcho->relativeBearingRadians, radius * normalizedRange);
        const float echoUncertaintyPixels = std::clamp(
            snapshot.recentEcho->rangeUncertaintyMeters / snapshot.displayRangeMeters * radius,
            2.0F,
            radius * 0.20F);
        drawList->AddCircle(echoPoint, echoUncertaintyPixels, echoColor, 24, 2.0F);
        drawList->AddLine(ImVec2(echoPoint.x - 6.0F, echoPoint.y - 6.0F),
                          ImVec2(echoPoint.x + 6.0F, echoPoint.y + 6.0F), echoColor, 2.0F);
        drawList->AddLine(ImVec2(echoPoint.x - 6.0F, echoPoint.y + 6.0F),
                          ImVec2(echoPoint.x + 6.0F, echoPoint.y - 6.0F), echoColor, 2.0F);
    }

    ImGui::TextUnformatted("Bearing-only = radial line | ranged = contact point");
    ImGui::End();
}

void DrawCameraScaleHud(const CameraScaleHudSnapshot& snapshot)
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (viewport != nullptr)
    {
        constexpr float margin = 16.0F;
        ImGui::SetNextWindowPos(
            ImVec2(viewport->WorkPos.x + margin, viewport->WorkPos.y + viewport->WorkSize.y - margin),
            ImGuiCond_Always,
            ImVec2(0.0F, 1.0F));
    }
    ImGui::SetNextWindowBgAlpha(0.78F);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize |
                                       ImGuiWindowFlags_NoDecoration |
                                       ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_NoNavInputs |
                                       ImGuiWindowFlags_NoInputs;
    if (!ImGui::Begin("##CAMERA_SCALE", nullptr, flags))
    {
        ImGui::End();
        return;
    }

    ImGui::Text("VIEW  %s", CameraBandName(snapshot.band));
    ImGui::Text("Scale: %.2f km", snapshot.horizontalSpanMeters / 1000.0F);
    ImGui::Text("Ownship: %.1f px", snapshot.ownshipProjectedPixels);
    ImGui::Text("Limit: %.2f km", snapshot.maximumHorizontalSpanMeters / 1000.0F);
    ImGui::End();
}

void DrawVesselNavigationHud(const VesselNavigationHudSnapshot& snapshot)
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (viewport != nullptr)
    {
        constexpr float margin = 16.0F;
        constexpr float cameraHudClearance = 112.0F;
        ImGui::SetNextWindowPos(
            ImVec2(viewport->WorkPos.x + margin,
                   viewport->WorkPos.y + viewport->WorkSize.y - margin - cameraHudClearance),
            ImGuiCond_Always,
            ImVec2(0.0F, 1.0F));
    }
    ImGui::SetNextWindowBgAlpha(0.78F);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize |
                                       ImGuiWindowFlags_NoDecoration |
                                       ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_NoNavInputs |
                                       ImGuiWindowFlags_NoInputs;
    if (!ImGui::Begin("##VESSEL_NAV", nullptr, flags))
    {
        ImGui::End();
        return;
    }
    ImGui::TextUnformatted("NAV");
    ImGui::Text("Depth: %.1f m", snapshot.signedDepthMeters);
    ImGui::Text("V/S: %+0.2f m/s (UP+)", snapshot.verticalSpeedMetersPerSecond);
    ImGui::Text("Throttle: %+0.0f%%", snapshot.throttleFraction * 100.0F);
    ImGui::Text("Planes B/S: %+0.0f%% / %+0.0f%%",
                snapshot.bowPlaneDeflectionFraction * 100.0F,
                snapshot.sternPlaneDeflectionFraction * 100.0F);
    ImGui::End();
}

void DrawTacticalSituationOverlay(
    const Camera::MultiScaleCameraBand band,
    const Render::OrthographicCamera& camera,
    const Physics::PhysicsVector3& ownshipPositionMeters,
    const std::optional<Physics::PhysicsVector3>& selectedTrackEstimatedPositionMeters,
    const std::optional<std::uint64_t>& selectedTrackId,
    const std::optional<Physics::PhysicsVector3>& playerTorpedoPositionMeters)
{
    if (band != Camera::MultiScaleCameraBand::Operational && band != Camera::MultiScaleCameraBand::Strategic)
    {
        return;
    }

    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    if (drawList == nullptr)
    {
        return;
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (viewport == nullptr)
    {
        return;
    }

    const ImU32 ownshipColor = IM_COL32(90, 220, 255, 255);
    const ImU32 trackColor = IM_COL32(255, 210, 80, 255);
    const ImU32 torpedoColor = IM_COL32(255, 245, 150, 255);
    const ImU32 labelColor = IM_COL32(235, 245, 255, 230);

    if (const auto ownship = ProjectWorldToMainViewport(camera, ownshipPositionMeters))
    {
        constexpr float radius = 6.0F;
        drawList->AddTriangleFilled(
            ImVec2(ownship->x + radius, ownship->y),
            ImVec2(ownship->x - radius, ownship->y - radius * 0.75F),
            ImVec2(ownship->x - radius, ownship->y + radius * 0.75F),
            ownshipColor);
        drawList->AddText(ImVec2(ownship->x + 9.0F, ownship->y - 7.0F), labelColor, "OWN");
    }

    if (selectedTrackEstimatedPositionMeters)
    {
        if (const auto track = ProjectWorldToMainViewport(camera, *selectedTrackEstimatedPositionMeters))
        {
            constexpr float radius = 7.0F;
            drawList->AddCircle(*track, radius, trackColor, 12, 2.0F);
            drawList->AddLine(ImVec2(track->x - 9.0F, track->y), ImVec2(track->x + 9.0F, track->y), trackColor, 1.0F);
            drawList->AddLine(ImVec2(track->x, track->y - 9.0F), ImVec2(track->x, track->y + 9.0F), trackColor, 1.0F);
            const std::string label = selectedTrackId
                ? "TRK #" + std::to_string(static_cast<unsigned long long>(*selectedTrackId))
                : std::string("TRK");
            drawList->AddText(ImVec2(track->x + 10.0F, track->y - 7.0F), labelColor, label.c_str());
        }
    }

    if (playerTorpedoPositionMeters)
    {
        if (const auto torpedo = ProjectWorldToMainViewport(camera, *playerTorpedoPositionMeters))
        {
            constexpr float radius = 5.0F;
            const ImVec2 points[4]{
                ImVec2(torpedo->x, torpedo->y - radius),
                ImVec2(torpedo->x + radius, torpedo->y),
                ImVec2(torpedo->x, torpedo->y + radius),
                ImVec2(torpedo->x - radius, torpedo->y)};
            drawList->AddConvexPolyFilled(points, 4, torpedoColor);
            drawList->AddText(ImVec2(torpedo->x + 8.0F, torpedo->y - 7.0F), labelColor, "TORP");
        }
    }

    const char* tierLabel = band == Camera::MultiScaleCameraBand::Strategic
        ? "STRATEGIC SYMBOL VIEW"
        : "OPERATIONAL SYMBOL VIEW";
    const ImVec2 textSize = ImGui::CalcTextSize(tierLabel);
    drawList->AddText(
        ImVec2(viewport->WorkPos.x + 0.5F * (viewport->WorkSize.x - textSize.x), viewport->WorkPos.y + 18.0F),
        IM_COL32(220, 235, 245, 210),
        tierLabel);
}
} // namespace DeepRun::Game::Combat
