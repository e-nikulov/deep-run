#include "Game/Combat/CombatCommandUi.h"

#include <imgui.h>

#include <cmath>
#include <optional>

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
    case PlayerCombatCommandType::PrepareWeapon: return "PREPARE WEAPON";
    case PlayerCombatCommandType::FireWeapon: return "FIRE WEAPON";
    case PlayerCombatCommandType::ActiveSonarPing: return "ACTIVE SONAR";
    case PlayerCombatCommandType::DeployDecoy: return "DEPLOY DECOY";
    }
    return "UNKNOWN COMMAND";
}
}

void DrawCombatCommandUi(const PlayerCombatPresentationSnapshot& snapshot)
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

    ImGui::Text("Weapon: %s", WeaponPhaseName(snapshot.weaponPhase));
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
    ImGui::TextUnformatted("LT / R / RMB     Prepare weapon");
    ImGui::TextUnformatted("RT / LMB         Fire weapon");
    ImGui::TextUnformatted("RB / Space       Active sonar ping");
    ImGui::TextUnformatted("X / F            Deploy decoy");
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
