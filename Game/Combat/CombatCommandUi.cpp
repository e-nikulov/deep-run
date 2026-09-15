#include "Game/Combat/CombatCommandUi.h"
#include "Game/Weapons/P700LauncherInventory.h"
#include "Simulation/Weapons/WeaponEmploymentEnvelope.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
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

const char* ClassificationName(const Perception::ContactClassification classification) noexcept
{
    switch (classification)
    {
    case Perception::ContactClassification::Unknown: return "UNCONFIRMED";
    case Perception::ContactClassification::MilitarySurfaceCombatant: return "MILITARY / VISUALLY CONFIRMED";
    case Perception::ContactClassification::CivilianSurfaceVessel: return "CIVILIAN / FIRE INHIBITED";
    }
    return "UNCONFIRMED";
}

const char* OpticalDetailName(const Perception::OpticalIdentificationLevel detail) noexcept
{
    switch (detail)
    {
    case Perception::OpticalIdentificationLevel::None: return "NONE";
    case Perception::OpticalIdentificationLevel::Detected: return "SILHOUETTE / TYPE UNRESOLVED";
    case Perception::OpticalIdentificationLevel::TypeResolved: return "TYPE RESOLVED";
    case Perception::OpticalIdentificationLevel::FlagOrMarkingsResolved: return "FLAG / MARKINGS RESOLVED";
    }
    return "NONE";
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

const Weapons::WeaponEmploymentEnvelope* SelectedWeaponEmploymentEnvelope(
    const Armament::PlayerWeaponType weapon) noexcept
{
    switch (weapon)
    {
    case Armament::PlayerWeaponType::HeavyweightTorpedo:
        return &Weapons::Uset80EmploymentEnvelope;
    case Armament::PlayerWeaponType::Type6576AFast:
        return &Weapons::Type6576AFastEmploymentEnvelope;
    case Armament::PlayerWeaponType::Type6576AEconomy:
        return &Weapons::Type6576AEconomyEmploymentEnvelope;
    case Armament::PlayerWeaponType::P700Granit:
        return &Weapons::P700GranitEmploymentEnvelope;
    }
    return nullptr;
}

std::optional<float> SelectedTrackRangeMeters(const PlayerCombatPresentationSnapshot& snapshot) noexcept
{
    if (!snapshot.selectedTrackId)
    {
        return std::nullopt;
    }
    for (const auto& track : snapshot.sonar.tracks)
    {
        if (track.trackId == *snapshot.selectedTrackId)
        {
            return track.estimatedRangeMeters;
        }
    }
    return std::nullopt;
}

// Depth-plane deflection is committed simulation state driven from the same canonical Depth command as the
// low-speed variable-ballast controller. Use it only to name the player's trim intent; vertical speed then
// distinguishes an actively damping neutral command from a genuinely settled/trimmed boat.
const char* BuoyancyTrimStateName(const VesselNavigationHudSnapshot& snapshot) noexcept
{
    constexpr float commandThreshold = 0.01F;
    constexpr float settlingVerticalSpeedThreshold = 0.05F;
    // Bow planes are deployment-only; stern deflection is the only hydrodynamic depth/pitch command cue.
    // Canonical Depth -1 (surface) produces negative stern deflection, +1 (dive) positive deflection.
    if (snapshot.sternPlaneDeflectionFraction < -commandThreshold)
    {
        return "INCREASING BUOYANCY";
    }
    if (snapshot.sternPlaneDeflectionFraction > commandThreshold)
    {
        return "DECREASING BUOYANCY";
    }
    if (std::abs(snapshot.verticalSpeedMetersPerSecond) > settlingVerticalSpeedThreshold)
    {
        return "STABILIZING";
    }
    return "NEUTRAL / TRIMMED";
}

const char* MainBallastStateName(const VesselNavigationHudSnapshot& snapshot) noexcept
{
    constexpr float flowThreshold = 1.0e-4F;
    if (snapshot.mainBallastFlowFractionPerSecond < -flowThreshold)
    {
        return "BLOWING";
    }
    if (snapshot.mainBallastFlowFractionPerSecond > flowThreshold)
    {
        return "FLOODING";
    }
    if (snapshot.mainBallastFillFraction <= 1.0e-3F)
    {
        return "EMPTY";
    }
    if (snapshot.mainBallastFillFraction >= 1.0F - 1.0e-3F)
    {
        return "FULL";
    }
    return "HOLDING";
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
    case PlayerCombatCommandType::TogglePeriscope: return "PERISCOPE";
    case PlayerCombatCommandType::VisualIdentify: return "VISUAL ID";
    case PlayerCombatCommandType::ToggleP700SalvoMode: return "P-700 SALVO MODE";
    case PlayerCombatCommandType::CycleElectronicSuite: return "ELECTRONICS SELECT";
    case PlayerCombatCommandType::OperateElectronicSuite: return "ELECTRONICS OPERATE";
    }
    return "UNKNOWN COMMAND";
}
}

void DrawCombatCommandUi(
    const PlayerCombatPresentationSnapshot& snapshot,
    CombatUiPresentationSettings* presentationSettings)
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (viewport != nullptr)
    {
        constexpr float margin = 16.0F;
        ImGui::SetNextWindowPos(
            ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - margin, viewport->WorkPos.y + margin),
            ImGuiCond_Always,
            ImVec2(1.0F, 0.0F));
    }
    ImGui::SetNextWindowSize(ImVec2(390.0F, 0.0F), ImGuiCond_FirstUseEver);
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
    ImGui::Text("P-700 salvo: %s (G / D-pad Down)",
        snapshot.p700SalvoMode == Weapons::P700SalvoMode::Pair ? "PAIR x2 / cooperative" : "SINGLE x1 / economical");
    if (snapshot.p700NextLaunchReadySeconds > 0.0)
        ImGui::Text("Next P-700 hatch sequence: %.1f s", snapshot.p700NextLaunchReadySeconds);
    if (snapshot.activeP700FloodProgress && *snapshot.activeP700FloodProgress < 1.0F)
        ImGui::Text("P-700 launcher flooding: %.0f%%", *snapshot.activeP700FloodProgress * 100.0F);
    ImGui::TextUnformatted("Torpedo tubes:");
    const auto drawTorpedoTubeCalibre = [&](const std::uint16_t calibreMillimeters, const std::size_t roundsRemaining)
    {
        ImGui::Text("%u mm / ammo %zu:", static_cast<unsigned int>(calibreMillimeters), roundsRemaining);
        for (const auto& tube : snapshot.torpedoTubes)
        {
            if (tube.calibreMillimeters != calibreMillimeters)
                continue;
            ImGui::SameLine(0.0F, 8.0F);
            if (tube.ready)
                ImGui::Text("#%zu READY", tube.tubeNumber);
            else
                ImGui::Text("#%zu RLD %.0fs", tube.tubeNumber, tube.reloadSecondsRemaining);
        }
    };
    drawTorpedoTubeCalibre(533U, snapshot.torpedo533RoundsRemaining);
    drawTorpedoTubeCalibre(650U, snapshot.torpedo650RoundsRemaining);
    if (snapshot.selectedWeapon != Armament::PlayerWeaponType::P700Granit)
    {
        ImGui::Text("Selected torpedo pool: %zu | ready tubes: %zu / %zu",
                    snapshot.torpedoRoundsRemaining, snapshot.torpedoReadyTubeCount, snapshot.torpedoTubeCount);
        if (snapshot.torpedoNextTubeReadySeconds)
            ImGui::Text("Next compatible tube reload: %.1f s", *snapshot.torpedoNextTubeReadySeconds);
    }
    ImGui::Text("In flight: torpedoes %zu | P-700 %zu",
                snapshot.playerTorpedoesInFlight, snapshot.playerP700InFlight);
    if (snapshot.weaponTargetTrackId)
    {
        ImGui::Text("Weapon track: #%llu", static_cast<unsigned long long>(*snapshot.weaponTargetTrackId));
    }

    ImGui::Separator();
    if (!snapshot.selectedTrackId)
    {
        ImGui::TextUnformatted("TARGET: ACQUIRING / NONE SELECTED");
        ImGui::TextUnformatted("Y / Tab cycles perceived sonar contacts");
        ImGui::TextUnformatted("Track quality: NO TRACK");
        ImGui::TextUnformatted("Engagement range: NO TRACK");
        ImGui::TextUnformatted("Identification: NO TRACK");
    }
    else
    {
        ImGui::Text("TARGET LOCK: #%llu", static_cast<unsigned long long>(*snapshot.selectedTrackId));
        if (!snapshot.selectedTrackPresent)
        {
            ImGui::TextUnformatted("Track state: NOT PRESENT - reacquiring next contact");
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

            ImGui::Text("Track quality: %s", snapshot.selectedTrackWeaponQualified ? "WEAPON QUALITY" : "INSUFFICIENT");
            const auto selectedRangeMeters = SelectedTrackRangeMeters(snapshot);
            const auto* envelope = SelectedWeaponEmploymentEnvelope(snapshot.selectedWeapon);
            if (!selectedRangeMeters)
            {
                ImGui::TextUnformatted("Engagement range: NEED RANGE - RB / Space active sonar");
            }
            else if (envelope == nullptr)
            {
                ImGui::Text("Engagement range: %.1f km", *selectedRangeMeters / 1000.0F);
            }
            else if (*selectedRangeMeters < envelope->minimumTargetRangeMeters)
            {
                ImGui::TextColored(
                    ImVec4(1.0F, 0.72F, 0.20F, 1.0F),
                    "Engagement range: %.1f km - NON-OPTIMAL / TOO CLOSE (%.1f km)",
                    *selectedRangeMeters / 1000.0F,
                    envelope->minimumTargetRangeMeters / 1000.0F);
                if (snapshot.selectedWeapon == Armament::PlayerWeaponType::P700Granit)
                    ImGui::TextWrapped("FIRE ALLOWED: short flight profile gives ship defenses a better intercept opportunity.");
                else
                    ImGui::TextWrapped("FIRE ALLOWED: compressed straight-run/seeker geometry increases acquisition or overshoot risk.");
            }
            else if (*selectedRangeMeters > envelope->maximumTargetRangeMeters)
            {
                ImGui::TextColored(
                    ImVec4(1.0F, 0.72F, 0.20F, 1.0F),
                    "Engagement range: %.1f km - NON-OPTIMAL / BEYOND %.1f km NOMINAL",
                    *selectedRangeMeters / 1000.0F,
                    envelope->maximumTargetRangeMeters / 1000.0F);
                ImGui::TextWrapped("FIRE ALLOWED: weapon may exhaust its travel/endurance budget before intercept and be lost.");
            }
            else
            {
                ImGui::Text("Engagement range: %.1f km - IN RANGE",
                            *selectedRangeMeters / 1000.0F);
            }

            ImGui::Text("Sources: %s%s%s%s%s%s",
                snapshot.selectedTrackHasPassiveAcousticEvidence ? "PASSIVE " : "",
                snapshot.selectedTrackHasActiveAcousticEvidence ? "ACTIVE " : "",
                snapshot.selectedTrackHasElectronicSupportEvidence ? "ESM " : "",
                snapshot.selectedTrackHasSurfaceRadarEvidence ? "RADAR " : "",
                snapshot.selectedTrackHasExternalReportEvidence ? "EXT-CU " : "",
                snapshot.selectedTrackHasOpticalEvidence ? "OPTICAL" : "");
            if (snapshot.selectedTrackExternalReportAgeSeconds)
                ImGui::Text("External-CU evidence age: %.0f s", *snapshot.selectedTrackExternalReportAgeSeconds);
            ImGui::Text("Optical detail: %s", OpticalDetailName(snapshot.selectedTrackOpticalIdentificationLevel));
            if (snapshot.selectedTrackVisuallyIdentified)
            {
                ImGui::Text("Identification: %s", ClassificationName(snapshot.selectedTrackClassification));
            }
            else if (snapshot.selectedTrackCivilianRisk)
            {
                ImGui::TextUnformatted("Identification: UNCONFIRMED - CIVILIAN RISK");
            }
            else
            {
                ImGui::TextUnformatted("Identification: UNCONFIRMED");
            }
        }
    }

    ImGui::Separator();
    ImGui::TextUnformatted("PERISCOPE");
    if (!snapshot.periscopeWithinOperatingDepth)
    {
        ImGui::TextUnformatted("State: UNAVAILABLE - TOO DEEP");
    }
    else
    {
        ImGui::Text("State: %s", snapshot.periscopeRaised ? "RAISED / MAST EXPOSED" : "STOWED");
    }
    if (snapshot.periscopeViewBearingRadians)
    {
        constexpr float radiansToDegrees = 57.2957795F;
        ImGui::Text("Optical bearing: %.1f deg", *snapshot.periscopeViewBearingRadians * radiansToDegrees);
    }
    ImGui::Text("Visual ID: %s", snapshot.canVisualIdentify ? "READY" : "UNAVAILABLE");

    ImGui::Separator();
    ImGui::TextUnformatted("DEVICE CONTROL");
    ImGui::TextDisabled("LB / R: select device     B / =: operate selected");

    const auto deviceStateLabel = [&snapshot](const AnteyElectronicSystem system) -> const char*
    {
        const bool deployed = snapshot.electronicSystemDeployed[AnteyElectronicSystemIndex(system)];
        if (!deployed)
            return "STOWED";
        switch (system)
        {
        case AnteyElectronicSystem::SynthesisSatNav: return "SAT FIX";
        case AnteyElectronicSystem::ZonaRadioDirectionFinder: return "PASSIVE ESM";
        case AnteyElectronicSystem::AnisRadio: return "RX READY";
        case AnteyElectronicSystem::Mrsc2TargetingReceiver: return "CU RECEIVE";
        case AnteyElectronicSystem::RadianSurfaceRadar:
            return snapshot.radianTransmitting ? "RADAR TX" : "STANDBY";
        case AnteyElectronicSystem::KoraCommunications: return "RX READY";
        case AnteyElectronicSystem::RkpCompressorIntake:
            return snapshot.rkpCompressorRunning ? "RUNNING" :
                   (snapshot.rkpCompressorRequested ? "REQUESTED" : "RAISED");
        case AnteyElectronicSystem::SelenaSatelliteTargeting: return "CU RECEIVE";
        case AnteyElectronicSystem::Signal3NavigationPeriscope: return "OPTICS";
        case AnteyElectronicSystem::Pzns10AttackPeriscope: return "OPTICS";
        case AnteyElectronicSystem::Count: break;
        }
        return "UNKNOWN";
    };

    if (ImGui::BeginChild("##ANTEY_DEVICE_CONTROL_MENU", ImVec2(0.0F, 225.0F), true,
                          ImGuiWindowFlags_NoNavInputs | ImGuiWindowFlags_AlwaysVerticalScrollbar))
    {
        for (std::size_t index = 0; index < AnteyElectronicSystemCount; ++index)
        {
            const auto system = static_cast<AnteyElectronicSystem>(index);
            const std::string_view name = AnteyElectronicSystemName(system);
            const bool selected = system == snapshot.selectedElectronicSystem;
            const char* state = deviceStateLabel(system);
            if (selected)
            {
                ImGui::TextColored(ImVec4(1.0F, 0.86F, 0.36F, 1.0F),
                                   "> %02zu  %.*s", index + 1U,
                                   static_cast<int>(name.size()), name.data());
                ImGui::SameLine();
                ImGui::TextColored(
                    snapshot.radianTransmitting && system == AnteyElectronicSystem::RadianSurfaceRadar
                        ? ImVec4(1.0F, 0.42F, 0.25F, 1.0F)
                        : ImVec4(0.72F, 0.90F, 1.0F, 1.0F),
                    "[%s]", state);
            }
            else
            {
                ImGui::Text("  %02zu  %.*s", index + 1U,
                            static_cast<int>(name.size()), name.data());
                ImGui::SameLine();
                ImGui::TextDisabled("[%s]", state);
            }
        }
    }
    ImGui::EndChild();

    const std::string_view selectedElectronicName = AnteyElectronicSystemName(snapshot.selectedElectronicSystem);
    const bool selectedElectronicRaised = snapshot.electronicSystemDeployed[
        AnteyElectronicSystemIndex(snapshot.selectedElectronicSystem)];
    const char* selectedAction = selectedElectronicRaised ? "STOW" : "RAISE";
    if (snapshot.selectedElectronicSystem == AnteyElectronicSystem::RadianSurfaceRadar && selectedElectronicRaised)
        selectedAction = snapshot.radianTransmitting ? "STOP RADAR TX" : "START RADAR TX";
    else if ((snapshot.selectedElectronicSystem == AnteyElectronicSystem::AnisRadio ||
              snapshot.selectedElectronicSystem == AnteyElectronicSystem::KoraCommunications) && selectedElectronicRaised)
        selectedAction = "BURST TRANSMIT";
    else if (snapshot.selectedElectronicSystem == AnteyElectronicSystem::Pzns10AttackPeriscope)
        selectedAction = snapshot.periscopeRaised ? "STOW WITH P / D-PAD UP" : "RAISE WITH P / D-PAD UP";

    ImGui::Text("Selected: %.*s", static_cast<int>(selectedElectronicName.size()), selectedElectronicName.data());
    ImGui::Text("Action: %s", selectedAction);
    ImGui::TextDisabled("Shared hardware: ZONA + RADIAN | KORA + RKP");

    if (snapshot.radianTransmitting)
        ImGui::TextColored(ImVec4(1.0F, 0.42F, 0.25F, 1.0F), "RADIAN: TRANSMITTING / ESM INTERCEPT RISK");
    else
        ImGui::TextUnformatted("RADIAN: EMCON");
    if (snapshot.radioTransmitting)
        ImGui::TextColored(ImVec4(1.0F, 0.42F, 0.25F, 1.0F), "Radio: BURST TX / ESM INTERCEPT RISK");
    else
        ImGui::TextUnformatted("Radio: SILENT");

    ImGui::Text("INS position error: +/- %.0f m", snapshot.navigationErrorMeters);
    if (snapshot.externalTargetReportSource)
    {
        const std::string_view reportSource = ExternalTargetReportSourceName(*snapshot.externalTargetReportSource);
        ImGui::Text("External CU: %.*s | age %.0f s | unc +/- %.1f km",
                    static_cast<int>(reportSource.size()), reportSource.data(),
                    snapshot.externalTargetReportAgeSeconds.value_or(0.0F),
                    snapshot.externalTargetReportUncertaintyMeters.value_or(0.0F) / 1000.0F);
    }
    else
        ImGui::TextUnformatted("External CU: NONE");
    ImGui::Text("HP air: %.0f%% | RKP: %s", snapshot.highPressureAirFraction * 100.0F,
                snapshot.rkpCompressorRunning ? "RUNNING" :
                (snapshot.rkpCompressorRequested ? "REQUESTED" : "OFF"));

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
    ImGui::TextUnformatted("Y / Tab          Next target");
    ImGui::TextUnformatted("D-pad Up / P     Raise/lower periscope");
    ImGui::TextUnformatted("A / V            Visual identify");
    ImGui::TextUnformatted("D-pad L/R / Z/C  Select weapon");
    ImGui::TextUnformatted("D-pad Down / G    P-700 single/pair");
    ImGui::TextUnformatted("RT / LMB         Fire weapon");
    ImGui::TextUnformatted("RB / Space       Range target / active sonar");
    ImGui::TextUnformatted("X / F            Deploy decoy");
    ImGui::TextUnformatted("LB / R           Select electronic/mast system");
    ImGui::TextUnformatted("B / =            Raise/operate selected system");
    ImGui::End();

    if (snapshot.periscopeRaised)
    {
        const ImGuiViewport* scopeViewport = ImGui::GetMainViewport();
        if (scopeViewport != nullptr)
        {
            ImGui::SetNextWindowPos(
                ImVec2(scopeViewport->WorkPos.x + scopeViewport->WorkSize.x * 0.5F,
                       scopeViewport->WorkPos.y + 16.0F),
                ImGuiCond_Always,
                ImVec2(0.5F, 0.0F));
        }
        ImGui::SetNextWindowSize(ImVec2(460.0F, 300.0F), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.92F);
        constexpr ImGuiWindowFlags scopeFlags = ImGuiWindowFlags_NoCollapse |
                                                ImGuiWindowFlags_NoSavedSettings |
                                                ImGuiWindowFlags_NoNavInputs |
                                                ImGuiWindowFlags_NoInputs;
        if (ImGui::Begin("PERISCOPE VIEW", nullptr, scopeFlags))
        {
            ImDrawList* draw = ImGui::GetWindowDrawList();
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            const ImVec2 size = ImGui::GetContentRegionAvail();
            const ImVec2 center(origin.x + size.x * 0.5F, origin.y + size.y * 0.5F - 10.0F);
            const float radius = (std::min)(size.x, size.y - 28.0F) * 0.42F;
            const ImU32 reticle = IM_COL32(205, 225, 205, 220);
            const ImU32 dim = IM_COL32(110, 135, 120, 180);
            draw->AddCircle(center, radius, reticle, 96, 1.5F);
            draw->AddLine(ImVec2(center.x - radius, center.y), ImVec2(center.x + radius, center.y), dim, 1.0F);
            draw->AddLine(ImVec2(center.x, center.y - radius), ImVec2(center.x, center.y + radius), dim, 1.0F);
            draw->AddLine(ImVec2(center.x - 14.0F, center.y), ImVec2(center.x + 14.0F, center.y), reticle, 2.0F);
            draw->AddLine(ImVec2(center.x, center.y - 10.0F), ImVec2(center.x, center.y + 10.0F), reticle, 2.0F);

            if (snapshot.selectedTrackPresent &&
                snapshot.selectedTrackOpticalIdentificationLevel != Perception::OpticalIdentificationLevel::None)
            {
                const float hullWidth = radius * 0.60F;
                const float hullY = center.y + radius * 0.20F;
                draw->AddLine(ImVec2(center.x - hullWidth * 0.5F, hullY),
                              ImVec2(center.x + hullWidth * 0.5F, hullY), reticle, 3.0F);
                draw->AddTriangleFilled(
                    ImVec2(center.x + hullWidth * 0.5F, hullY),
                    ImVec2(center.x + hullWidth * 0.34F, hullY - 7.0F),
                    ImVec2(center.x + hullWidth * 0.34F, hullY + 3.0F), reticle);
                draw->AddRectFilled(
                    ImVec2(center.x - hullWidth * 0.12F, hullY - 13.0F),
                    ImVec2(center.x + hullWidth * 0.10F, hullY), reticle);
            }

            ImGui::SetCursorScreenPos(ImVec2(origin.x + 8.0F, origin.y + size.y - 24.0F));
            ImGui::Text("BRG %.1f deg | %s | %s",
                        snapshot.periscopeViewBearingRadians.value_or(0.0F) * 57.2957795F,
                        OpticalDetailName(snapshot.selectedTrackOpticalIdentificationLevel),
                        snapshot.selectedTrackVisuallyIdentified
                            ? ClassificationName(snapshot.selectedTrackClassification)
                            : "UNCONFIRMED");
        }
        ImGui::End();
    }
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
    ImGui::SetNextWindowSize(ImVec2(380.0F, 430.0F), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.90F);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse |
                                       ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_NoNavInputs |
                                       ImGuiWindowFlags_NoInputs;
    if (!ImGui::Begin("SONAR", nullptr, flags))
    {
        ImGui::End();
        return;
    }

    ImGui::Text("PASSIVE 360 | Scale %.1f km", snapshot.displayRangeMeters / 1000.0F);
    ImGui::SameLine();
    ImGui::Text("Contacts: %d", static_cast<int>(snapshot.tracks.size()));
    if (snapshot.activePulse)
    {
        ImGui::TextUnformatted("ACTIVE: DIRECTED PING OUT");
    }
    else if (snapshot.recentEcho)
    {
        ImGui::TextUnformatted("ACTIVE: ECHO RECEIVED");
    }
    else
    {
        ImGui::TextUnformatted("ACTIVE: STANDBY");
    }

    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float scopeSize = std::max(160.0F, std::min(available.x, available.y - 58.0F));
    const ImVec2 topLeft = ImGui::GetCursorScreenPos();
    const ImVec2 center(topLeft.x + scopeSize * 0.5F, topLeft.y + scopeSize * 0.5F);
    const float radius = scopeSize * 0.42F;
    ImGui::InvisibleButton("##SONAR_SCOPE", ImVec2(scopeSize, scopeSize));
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    if (drawList == nullptr || snapshot.displayRangeMeters <= 0.0F)
    {
        ImGui::End();
        return;
    }

    constexpr float pi = 3.14159265358979323846F;
    constexpr float degreesToRadians = pi / 180.0F;
    const ImU32 gridColor = IM_COL32(90, 135, 145, 120);
    const ImU32 textColor = IM_COL32(185, 225, 230, 220);
    const ImU32 passiveColor = IM_COL32(105, 235, 190, 235);
    const ImU32 passiveDimColor = IM_COL32(105, 235, 190, 80);
    const ImU32 rangedColor = IM_COL32(115, 215, 245, 245);
    const ImU32 rangedDimColor = IM_COL32(115, 215, 245, 100);
    const ImU32 selectedColor = IM_COL32(255, 220, 100, 255);
    const ImU32 pingColor = IM_COL32(90, 210, 255, 210);
    const ImU32 echoColor = IM_COL32(255, 175, 90, 240);

    // Present the accepted bow-relative 2.5D acoustic bearing as a polar scope. The current simulation bearing
    // lives in the XY gameplay plane, so 90/270 are intentionally not labelled STBD/PORT until a true XZ
    // azimuth channel exists; doing so here would invent spatial information the Track does not own.
    const auto pointAt = [&center](const float bearingRadians, const float distancePixels) {
        return ImVec2(center.x + std::sin(bearingRadians) * distancePixels,
                      center.y - std::cos(bearingRadians) * distancePixels);
    };
    const auto screenAngle = [](const float bearingRadians) {
        constexpr float halfPi = 1.57079632679489661923F;
        return bearingRadians - halfPi;
    };

    for (int ring = 1; ring <= 4; ++ring)
    {
        drawList->AddCircle(center, radius * (static_cast<float>(ring) / 4.0F), gridColor, 64, 1.0F);
    }
    for (int tick = 0; tick < 12; ++tick)
    {
        const float bearing = static_cast<float>(tick) * 30.0F * degreesToRadians;
        const float inner = radius - ((tick % 3) == 0 ? 8.0F : 4.0F);
        drawList->AddLine(pointAt(bearing, inner), pointAt(bearing, radius + 1.0F), gridColor, 1.0F);
    }
    drawList->AddLine(pointAt(0.0F, 0.0F), pointAt(0.0F, radius), gridColor, 1.0F);
    drawList->AddLine(pointAt(0.5F * pi, 0.0F), pointAt(0.5F * pi, radius), gridColor, 1.0F);
    drawList->AddLine(pointAt(pi, 0.0F), pointAt(pi, radius), gridColor, 1.0F);
    drawList->AddLine(pointAt(-0.5F * pi, 0.0F), pointAt(-0.5F * pi, radius), gridColor, 1.0F);

    drawList->AddText(ImVec2(center.x - 18.0F, center.y - radius - 18.0F), textColor, "0 BOW");
    drawList->AddText(ImVec2(center.x + radius + 4.0F, center.y - 7.0F), textColor, "90");
    drawList->AddText(ImVec2(center.x - 26.0F, center.y + radius + 3.0F), textColor, "180 AFT");
    drawList->AddText(ImVec2(center.x - radius - 28.0F, center.y - 7.0F), textColor, "270");

    const ImVec2 ownshipTriangle[3]{
        ImVec2(center.x, center.y - 8.0F),
        ImVec2(center.x - 5.0F, center.y + 6.0F),
        ImVec2(center.x + 5.0F, center.y + 6.0F)};
    drawList->AddConvexPolyFilled(ownshipTriangle, 3, textColor);

    for (const auto& contact : snapshot.tracks)
    {
        const bool ranged = contact.estimatedRangeMeters.has_value();
        const ImU32 baseColor = ranged ? rangedColor : passiveColor;
        const ImU32 dimColor = ranged ? rangedDimColor : passiveDimColor;
        const ImU32 color = contact.selected ? selectedColor : baseColor;
        const float bearing = contact.relativeBearingRadians;
        const float uncertainty = std::min(contact.bearingUncertaintyRadians, 1.2F);
        const float contactRadius = ranged
            ? radius * std::clamp(*contact.estimatedRangeMeters / snapshot.displayRangeMeters, 0.0F, 1.0F)
            : radius * 0.96F;
        const ImVec2 contactPoint = pointAt(bearing, contactRadius);

        if (!ranged)
        {
            // Passive-only evidence is a bearing fan. The perimeter marker is deliberately hollow so the outer
            // ring cannot be read as a fabricated target range.
            const float fanInnerRadius = radius * 0.14F;
            drawList->AddLine(pointAt(bearing, fanInnerRadius), contactPoint,
                              color, contact.selected ? 2.0F : 1.0F);
            drawList->AddLine(pointAt(bearing - uncertainty, fanInnerRadius),
                              pointAt(bearing - uncertainty, contactRadius), dimColor, 1.0F);
            drawList->AddLine(pointAt(bearing + uncertainty, fanInnerRadius),
                              pointAt(bearing + uncertainty, contactRadius), dimColor, 1.0F);
            drawList->AddCircle(contactPoint, contact.selected ? 5.5F : 4.0F,
                                color, 12, contact.selected ? 2.0F : 1.5F);
        }
        else
        {
            if (contact.positionUncertaintyMeters)
            {
                const float uncertaintyPixels = std::clamp(
                    *contact.positionUncertaintyMeters / snapshot.displayRangeMeters * radius,
                    2.0F,
                    radius * 0.25F);
                drawList->AddCircle(contactPoint, uncertaintyPixels, dimColor, 24, 1.0F);
            }
            drawList->AddCircleFilled(contactPoint, contact.selected ? 5.5F : 4.0F, color, 16);
            drawList->AddLine(pointAt(bearing - uncertainty, contactRadius),
                              pointAt(bearing + uncertainty, contactRadius), dimColor, 1.0F);
        }

        if (contact.selected)
        {
            drawList->AddCircle(contactPoint, 10.0F, selectedColor, 20, 2.0F);
            drawList->AddLine(ImVec2(contactPoint.x - 14.0F, contactPoint.y),
                              ImVec2(contactPoint.x - 7.0F, contactPoint.y), selectedColor, 2.0F);
            drawList->AddLine(ImVec2(contactPoint.x + 7.0F, contactPoint.y),
                              ImVec2(contactPoint.x + 14.0F, contactPoint.y), selectedColor, 2.0F);
            drawList->AddLine(ImVec2(contactPoint.x, contactPoint.y - 14.0F),
                              ImVec2(contactPoint.x, contactPoint.y - 7.0F), selectedColor, 2.0F);
            drawList->AddLine(ImVec2(contactPoint.x, contactPoint.y + 7.0F),
                              ImVec2(contactPoint.x, contactPoint.y + 14.0F), selectedColor, 2.0F);
        }

        const char* knowledge = "BRG";
        switch (contact.knowledge)
        {
        case ContactKnowledgeLevel::BearingOnly: knowledge = "BRG"; break;
        case ContactKnowledgeLevel::AreaEstimate: knowledge = "AREA"; break;
        case ContactKnowledgeLevel::Classified: knowledge = "CLASS"; break;
        case ContactKnowledgeLevel::PositiveIdentification: knowledge = "ID"; break;
        case ContactKnowledgeLevel::Coasting: knowledge = "COAST"; break;
        }
        const std::string source = ranged ? "RNG" : "PAS";
        const std::string label = contact.selected
            ? "TARGET #" + std::to_string(static_cast<unsigned long long>(contact.trackId)) + " " + source + "/" + knowledge
            : "#" + std::to_string(static_cast<unsigned long long>(contact.trackId)) + " " + source + "/" + knowledge;
        drawList->AddText(ImVec2(contactPoint.x + 8.0F, contactPoint.y - 9.0F), color, label.c_str());
    }

    if (snapshot.activePulse)
    {
        const float waveRangeMeters = SonarOutgoingWaveRangeMeters(snapshot);
        const float waveRadius = radius * std::clamp(waveRangeMeters / snapshot.displayRangeMeters, 0.0F, 1.0F);
        const float bearing = snapshot.activePulse->relativeBearingRadians;
        const float halfAngle = snapshot.activePulse->beamHalfAngleRadians;
        drawList->AddLine(center, pointAt(bearing - halfAngle, radius), pingColor, 1.0F);
        drawList->AddLine(center, pointAt(bearing + halfAngle, radius), pingColor, 1.0F);
        if (waveRadius > 1.0F)
        {
            drawList->PathClear();
            drawList->PathArcTo(
                center,
                waveRadius,
                screenAngle(bearing - halfAngle),
                screenAngle(bearing + halfAngle),
                24);
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
        drawList->AddText(ImVec2(echoPoint.x + 9.0F, echoPoint.y + 7.0F), echoColor, "ACTIVE ECHO");
    }

    ImGui::TextUnformatted("Y / Tab cycles TARGET | amber reticle = selected | RB / Space = active range");
    ImGui::TextUnformatted("PAS=passive bearing | RNG=ranged/fused track | orange X=active echo");
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
    const float currentDepthMeters = std::max(0.0F, snapshot.signedDepthMeters);
    ImGui::TextUnformatted("NAV");
    ImGui::Text("Current depth: %.1f m", currentDepthMeters);
    ImGui::Text("Vertical speed: %+0.2f m/s (UP+)", snapshot.verticalSpeedMetersPerSecond);
    ImGui::Text("Axial speed: %+0.1f kn / %+0.2f m/s",
                snapshot.forwardSpeedMetersPerSecond * 1.94384449F,
                snapshot.forwardSpeedMetersPerSecond);
    ImGui::Text("Buoyancy / trim: %s", BuoyancyTrimStateName(snapshot));
    ImGui::Text("Main ballast: %.0f%% / %s | Trim: %+0.1f t",
                snapshot.mainBallastFillFraction * 100.0F,
                MainBallastStateName(snapshot),
                snapshot.trimMassDeltaKg / 1000.0F);
    ImGui::Text("HP air: %.0f%% | RKP compressor: %s",
                snapshot.highPressureAirFraction * 100.0F,
                snapshot.rkpCompressorRunning ? "RUNNING" : "OFF");
    if (snapshot.mainBallastFlowFractionPerSecond < -1.0e-4F)
    {
        ImGui::TextUnformatted("Surface hold (W / LS UP): MAIN BALLAST BLOW");
    }
    ImGui::Text("Physical mass: %.0f t", snapshot.dynamicMassKg / 1000.0F);
    ImGui::Text("Throttle: %+0.0f%%", snapshot.throttleFraction * 100.0F);
    ImGui::Text("Bow planes: %s", snapshot.bowPlanesDeployed ? "DEPLOYED / FIXED" : "STOWED");
    ImGui::Text("Stern planes: %+0.0f%%", snapshot.sternPlaneDeflectionFraction * 100.0F);
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
                ? "TARGET #" + std::to_string(static_cast<unsigned long long>(*selectedTrackId))
                : std::string("TARGET");
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
