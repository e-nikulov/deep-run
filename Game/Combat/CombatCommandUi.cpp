#include "Game/Combat/CombatCommandUi.h"

#include <imgui.h>

#include <cmath>

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
    ImGui::SetNextWindowPos(ImVec2(16.0F, 150.0F), ImGuiCond_FirstUseEver);
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
} // namespace DeepRun::Game::Combat
