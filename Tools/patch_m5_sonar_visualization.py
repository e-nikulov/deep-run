from pathlib import Path


def replace_once(path, old, new):
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected exactly one match, got {count}")
    p.write_text(text.replace(old, new, 1), encoding="utf-8")


sonar = r'''#pragma once

#include "Simulation/Acoustics/ActiveSonar.h"
#include "Simulation/Perception/TrackManager.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace DeepRun::Game::Combat
{
inline constexpr float M5SonarScopeMinimumRangeMeters = 2'000.0F;
inline constexpr float M5SonarScopeMaximumRangeMeters = 50'000.0F;
inline constexpr double M5SonarEchoPersistenceSeconds = 4.0;

struct SonarTrackPresentation final
{
    std::uint64_t trackId = 0;
    Perception::TrackLifecycleState lifecycle = Perception::TrackLifecycleState::Tentative;
    float relativeBearingRadians = 0.0F;
    float bearingUncertaintyRadians = 0.0F;
    float confidence = 0.0F;
    std::optional<float> estimatedRangeMeters{};
    std::optional<float> positionUncertaintyMeters{};
    bool selected = false;
};

struct SonarActivePulsePresentation final
{
    float relativeBearingRadians = 0.0F;
    float beamHalfAngleRadians = 0.0F;
    double emissionTimeSeconds = 0.0;
};

struct SonarEchoPresentation final
{
    float relativeBearingRadians = 0.0F;
    float bearingUncertaintyRadians = 0.0F;
    float estimatedRangeMeters = 0.0F;
    float rangeUncertaintyMeters = 0.0F;
    float confidence = 0.0F;
    double observationTimeSeconds = 0.0;
};

struct SonarPresentationSnapshot final
{
    std::vector<SonarTrackPresentation> tracks{};
    std::optional<SonarActivePulsePresentation> activePulse{};
    std::optional<SonarEchoPresentation> recentEcho{};
    float displayRangeMeters = M5SonarScopeMinimumRangeMeters;
    float ownshipHeadingRadians = 0.0F;
    double simulationTimeSeconds = 0.0;
};

[[nodiscard]] inline float WrapSonarAngle(const float angleRadians) noexcept
{
    constexpr float pi = 3.14159265358979323846F;
    constexpr float twoPi = 2.0F * pi;
    float wrapped = std::fmod(angleRadians + pi, twoPi);
    if (wrapped < 0.0F)
    {
        wrapped += twoPi;
    }
    return wrapped - pi;
}

// Presentation-only projection. Inputs are ownship state, perceived Tracks, the player's own transmitted pulse,
// and measured echo evidence. No hostile transform/body/entity identity is accepted by this boundary.
[[nodiscard]] inline std::expected<SonarPresentationSnapshot, std::string> BuildSonarPresentation(
    const std::span<const Perception::Track> tracks,
    const std::optional<std::uint64_t> selectedTrackId,
    const Physics::PhysicsVector3& ownshipPositionMeters,
    const float ownshipHeadingRadians,
    const std::optional<Acoustics::ActiveAcousticPulse>& activePulse,
    const std::optional<Acoustics::AcousticObservation>& recentActiveEcho,
    const double simulationTimeSeconds)
{
    if (!ownshipPositionMeters.IsFinite() || !std::isfinite(ownshipHeadingRadians) ||
        !std::isfinite(simulationTimeSeconds) || simulationTimeSeconds < 0.0)
    {
        return std::unexpected("M5 sonar presentation ownship/time input is invalid");
    }

    SonarPresentationSnapshot result{
        .ownshipHeadingRadians = ownshipHeadingRadians,
        .simulationTimeSeconds = simulationTimeSeconds};
    result.tracks.reserve(tracks.size());

    float requestedRangeMeters = M5SonarScopeMinimumRangeMeters;
    for (const auto& track : tracks)
    {
        if (track.lifecycle == Perception::TrackLifecycleState::Lost)
        {
            continue;
        }
        if (track.trackId == 0U || !std::isfinite(track.estimatedBearingRadians) ||
            !std::isfinite(track.bearingUncertaintyRadians) || track.bearingUncertaintyRadians < 0.0F ||
            !std::isfinite(track.confidence) || track.confidence < 0.0F || track.confidence > 1.0F)
        {
            return std::unexpected("M5 sonar presentation received an invalid perceived Track");
        }

        SonarTrackPresentation contact{
            .trackId = track.trackId,
            .lifecycle = track.lifecycle,
            .relativeBearingRadians = WrapSonarAngle(track.estimatedBearingRadians - ownshipHeadingRadians),
            .bearingUncertaintyRadians = track.bearingUncertaintyRadians,
            .confidence = track.confidence,
            .positionUncertaintyMeters = track.positionUncertaintyMeters,
            .selected = selectedTrackId.has_value() && *selectedTrackId == track.trackId};

        if (track.positionUncertaintyMeters &&
            (!std::isfinite(*track.positionUncertaintyMeters) || *track.positionUncertaintyMeters < 0.0F))
        {
            return std::unexpected("M5 sonar presentation received invalid perceived position uncertainty");
        }
        if (track.estimatedPositionMeters)
        {
            if (!track.estimatedPositionMeters->IsFinite())
            {
                return std::unexpected("M5 sonar presentation received an invalid perceived position");
            }
            const double dx = static_cast<double>(track.estimatedPositionMeters->x) - ownshipPositionMeters.x;
            const double dy = static_cast<double>(track.estimatedPositionMeters->y) - ownshipPositionMeters.y;
            const double dz = static_cast<double>(track.estimatedPositionMeters->z) - ownshipPositionMeters.z;
            const double rangeMeters = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (!std::isfinite(rangeMeters))
            {
                return std::unexpected("M5 sonar presentation perceived range is non-finite");
            }
            contact.estimatedRangeMeters = static_cast<float>(rangeMeters);
            const float uncertainty = track.positionUncertaintyMeters.value_or(0.0F);
            requestedRangeMeters = std::max(
                requestedRangeMeters,
                static_cast<float>(rangeMeters) * 1.20F + uncertainty);
        }
        result.tracks.push_back(contact);
    }

    if (activePulse)
    {
        if (!activePulse->forwardUnitVector.IsFinite() || !std::isfinite(activePulse->beamHalfAngleRadians) ||
            activePulse->beamHalfAngleRadians <= 0.0F || !std::isfinite(activePulse->emissionTimeSeconds) ||
            activePulse->emissionTimeSeconds < 0.0 || activePulse->emissionTimeSeconds > simulationTimeSeconds + 1.0e-6)
        {
            return std::unexpected("M5 sonar presentation received an invalid own active pulse");
        }
        const float absoluteBearing = static_cast<float>(std::atan2(
            static_cast<double>(activePulse->forwardUnitVector.y),
            static_cast<double>(activePulse->forwardUnitVector.x)));
        result.activePulse = SonarActivePulsePresentation{
            .relativeBearingRadians = WrapSonarAngle(absoluteBearing - ownshipHeadingRadians),
            .beamHalfAngleRadians = activePulse->beamHalfAngleRadians,
            .emissionTimeSeconds = activePulse->emissionTimeSeconds};
    }

    if (recentActiveEcho && recentActiveEcho->kind == Acoustics::AcousticObservationKind::ActiveEcho)
    {
        const double ageSeconds = simulationTimeSeconds - recentActiveEcho->observationTimeSeconds;
        if (ageSeconds >= -1.0e-6 && ageSeconds <= M5SonarEchoPersistenceSeconds &&
            recentActiveEcho->estimatedRangeMeters && recentActiveEcho->rangeUncertaintyMeters)
        {
            if (!std::isfinite(recentActiveEcho->measuredBearingRadians) ||
                !std::isfinite(recentActiveEcho->bearingUncertaintyRadians) ||
                recentActiveEcho->bearingUncertaintyRadians < 0.0F ||
                !std::isfinite(*recentActiveEcho->estimatedRangeMeters) || *recentActiveEcho->estimatedRangeMeters < 0.0F ||
                !std::isfinite(*recentActiveEcho->rangeUncertaintyMeters) || *recentActiveEcho->rangeUncertaintyMeters < 0.0F ||
                !std::isfinite(recentActiveEcho->confidence) || recentActiveEcho->confidence < 0.0F ||
                recentActiveEcho->confidence > 1.0F)
            {
                return std::unexpected("M5 sonar presentation received invalid active echo evidence");
            }
            result.recentEcho = SonarEchoPresentation{
                .relativeBearingRadians = WrapSonarAngle(
                    recentActiveEcho->measuredBearingRadians - ownshipHeadingRadians),
                .bearingUncertaintyRadians = recentActiveEcho->bearingUncertaintyRadians,
                .estimatedRangeMeters = *recentActiveEcho->estimatedRangeMeters,
                .rangeUncertaintyMeters = *recentActiveEcho->rangeUncertaintyMeters,
                .confidence = recentActiveEcho->confidence,
                .observationTimeSeconds = recentActiveEcho->observationTimeSeconds};
            requestedRangeMeters = std::max(
                requestedRangeMeters,
                recentActiveEcho->estimatedRangeMeters.value() * 1.20F + recentActiveEcho->rangeUncertaintyMeters.value());
        }
    }

    result.displayRangeMeters = std::clamp(
        requestedRangeMeters, M5SonarScopeMinimumRangeMeters, M5SonarScopeMaximumRangeMeters);
    return result;
}
} // namespace DeepRun::Game::Combat
'''
Path("Game/Combat/SonarPresentation.h").write_text(sonar, encoding="utf-8")

replace_once(
    "Game/Combat/PlayerCombatCommandRuntime.h",
    '#include "Simulation/Weapons/WeaponRuntime.h"\n',
    '#include "Game/Combat/SonarPresentation.h"\n#include "Simulation/Weapons/WeaponRuntime.h"\n',
)
replace_once(
    "Game/Combat/PlayerCombatCommandRuntime.h",
    "    bool canActiveSonarPing = false;\n    bool activeSonarPulsePending = false;\n",
    "    bool canActiveSonarPing = false;\n    bool activeSonarPulsePending = false;\n    SonarPresentationSnapshot sonar{};\n",
)

replace_once(
    "Game/Combat/CombatPlaygroundRuntime.h",
    """            if (activeEcho->has_value())
            {
                const auto perceived = Perception::FromAcousticObservation(
                    **activeEcho, activePulse_->originMeters);""",
    """            if (activeEcho->has_value())
            {
                lastPlayerActiveEchoObservation_ = **activeEcho;
                const auto perceived = Perception::FromAcousticObservation(
                    **activeEcho, activePulse_->originMeters);""",
)
replace_once(
    "Game/Combat/CombatPlaygroundRuntime.h",
    """        playerCombatPresentation.canActiveSonarPing = selectedPlayerTrack.has_value() &&
            selectedPlayerTrack->lifecycle != Perception::TrackLifecycleState::Lost &&
            !activePulse_.has_value() && simulationTimeSeconds + 1.0e-9 >= nextActivePulseTimeSeconds_;
        playerCombatPresentation.activeSonarPulsePending = activePulse_.has_value();
        playerCombatPresentation.canDeployDecoy = playerDecoyAvailable_;""",
    """        playerCombatPresentation.canActiveSonarPing = selectedPlayerTrack.has_value() &&
            selectedPlayerTrack->lifecycle != Perception::TrackLifecycleState::Lost &&
            !activePulse_.has_value() && simulationTimeSeconds + 1.0e-9 >= nextActivePulseTimeSeconds_;
        playerCombatPresentation.activeSonarPulsePending = activePulse_.has_value();

        float sonarOwnshipHeadingRadians = 0.0F;
        if (currentPlayerPhysicalProxy_.has_value())
        {
            const auto& orientation = currentPlayerPhysicalProxy_->orientation;
            sonarOwnshipHeadingRadians = static_cast<float>(std::atan2(
                2.0 * (static_cast<double>(orientation.w) * orientation.z +
                       static_cast<double>(orientation.x) * orientation.y),
                1.0 - 2.0 * (static_cast<double>(orientation.y) * orientation.y +
                             static_cast<double>(orientation.z) * orientation.z)));
            if (currentPlayerPhysicalProxy_->gameplayLongitudinalFacingSign < 0.0F)
            {
                sonarOwnshipHeadingRadians += 3.14159265358979323846F;
            }
        }
        const auto sonarPresentation = BuildSonarPresentation(
            playerTrackSnapshot,
            playerCombat_.SelectedTrackId(),
            playerSnapshot.passiveReceiver.positionMeters,
            sonarOwnshipHeadingRadians,
            activePulse_,
            lastPlayerActiveEchoObservation_,
            simulationTimeSeconds);
        if (!sonarPresentation)
        {
            return std::unexpected("M5-V2 sonar presentation projection failed: " + sonarPresentation.error());
        }
        playerCombatPresentation.sonar = *sonarPresentation;
        playerCombatPresentation.canDeployDecoy = playerDecoyAvailable_;""",
)
replace_once(
    "Game/Combat/CombatPlaygroundRuntime.h",
    """    std::optional<Acoustics::ActiveAcousticPulse> activePulse_{};
    std::optional<Acoustics::AcousticReflector> activeReflector_{};
    double nextActivePulseTimeSeconds_ = 0.0;""",
    """    std::optional<Acoustics::ActiveAcousticPulse> activePulse_{};
    std::optional<Acoustics::AcousticReflector> activeReflector_{};
    std::optional<Acoustics::AcousticObservation> lastPlayerActiveEchoObservation_{};
    double nextActivePulseTimeSeconds_ = 0.0;""",
)

replace_once(
    "Game/Combat/CombatCommandUi.h",
    "void DrawCombatCommandUi(const PlayerCombatPresentationSnapshot& snapshot);\n",
    "void DrawCombatCommandUi(const PlayerCombatPresentationSnapshot& snapshot);\nvoid DrawSonarScope(const SonarPresentationSnapshot& snapshot);\n",
)

ui = Path("Game/Combat/CombatCommandUi.cpp")
text = ui.read_text(encoding="utf-8")
anchor = "\nvoid DrawCameraScaleHud(const CameraScaleHudSnapshot& snapshot)\n"
if text.count(anchor) != 1:
    raise SystemExit("CombatCommandUi.cpp: DrawCameraScaleHud anchor mismatch")
sonar_ui = r'''
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
'''
ui.write_text(text.replace(anchor, "\n" + sonar_ui + anchor, 1), encoding="utf-8")

replace_once(
    "DeepRun/Main.cpp",
    """                        DeepRun::Game::Combat::DrawCombatCommandUi(*combatUiSnapshot);
                        const auto ownshipLengthMeters = playground.ProductionSubmarinePresentationLengthMeters();""",
    """                        DeepRun::Game::Combat::DrawCombatCommandUi(*combatUiSnapshot);
                        DeepRun::Game::Combat::DrawSonarScope(combatUiSnapshot->sonar);
                        const auto ownshipLengthMeters = playground.ProductionSubmarinePresentationLengthMeters();""",
)

replace_once(
    "Tests/M5WeaponRuntimeTest.cpp",
    '#include "Simulation/Acoustics/ActiveSonar.h"\n',
    '#include "Game/Combat/SonarPresentation.h"\n#include "Simulation/Acoustics/ActiveSonar.h"\n',
)
replace_once(
    "Tests/M5WeaponRuntimeTest.cpp",
    "#include <cmath>\n",
    "#include <array>\n#include <cmath>\n",
)

test_anchor = '''    auto agedSpatialTrack = activeSpatialTrack;
    agedSpatialTrack.positionUncertaintyMeters = 160.0F;
    Require(!ValidateTrackForWeapon(torpedo, agedSpatialTrack).has_value(),
            "weapon must reject spatial evidence after its uncertainty exceeds the authored budget");

'''
test_insert = test_anchor + r'''    {
        auto bearingOnlySonarTrack = MakeTrack();
        bearingOnlySonarTrack.trackId = 51U;
        bearingOnlySonarTrack.estimatedBearingRadians = 0.50F;
        bearingOnlySonarTrack.estimatedPositionMeters.reset();
        bearingOnlySonarTrack.positionUncertaintyMeters.reset();

        auto rangedSonarTrack = MakeTrack();
        rangedSonarTrack.trackId = 52U;
        rangedSonarTrack.estimatedBearingRadians = 0.25F;
        rangedSonarTrack.estimatedPositionMeters = DeepRun::Physics::PhysicsVector3{.x = 1100.0F, .y = -100.0F, .z = 0.0F};
        rangedSonarTrack.positionUncertaintyMeters = 75.0F;

        const std::array sonarTracks{bearingOnlySonarTrack, rangedSonarTrack};
        const DeepRun::Physics::PhysicsVector3 ownship{.x = 100.0F, .y = -100.0F, .z = 0.0F};
        const DeepRun::Acoustics::ActiveAcousticPulse pulse{
            .originMeters = ownship,
            .forwardUnitVector = {.x = 0.0F, .y = 1.0F, .z = 0.0F},
            .beamHalfAngleRadians = 0.25F,
            .emissionTimeSeconds = 40.0};
        const DeepRun::Acoustics::AcousticObservation echo{
            .kind = DeepRun::Acoustics::AcousticObservationKind::ActiveEcho,
            .sensorId = "MGK540_BOW_ARRAY",
            .observationTimeSeconds = 41.0,
            .arrivalTimeSeconds = 41.0,
            .measuredBearingRadians = 0.25F,
            .bearingUncertaintyRadians = 0.03F,
            .estimatedRangeMeters = 1000.0F,
            .rangeUncertaintyMeters = 20.0F,
            .confidence = 0.85F};

        const auto sonar = DeepRun::Game::Combat::BuildSonarPresentation(
            sonarTracks, 51U, ownship, 0.25F, pulse, echo, 42.0);
        Require(sonar.has_value(), "M5 sonar presentation must accept perceived tracks and own active evidence");
        Require(sonar->tracks.size() == 2U, "M5 sonar presentation must retain all non-lost perceived tracks");
        Require(sonar->tracks[0].selected && !sonar->tracks[0].estimatedRangeMeters.has_value(),
                "bearing-only sonar contact must remain bearing-only and selectable");
        Require(std::abs(sonar->tracks[0].relativeBearingRadians - 0.25F) < 0.001F,
                "sonar bearing must be relative to current ownship facing");
        Require(sonar->tracks[1].estimatedRangeMeters.has_value() &&
                    std::abs(*sonar->tracks[1].estimatedRangeMeters - 1000.0F) < 0.01F,
                "ranged sonar contact must derive range only from the perceived spatial estimate");
        Require(sonar->activePulse.has_value() && sonar->recentEcho.has_value(),
                "own ping and measured echo evidence must be visible to sonar presentation");
        Require(std::abs(sonar->recentEcho->estimatedRangeMeters - 1000.0F) < 0.01F,
                "sonar echo range must come from measured AcousticObservation evidence");

        const auto staleEcho = DeepRun::Game::Combat::BuildSonarPresentation(
            sonarTracks, 51U, ownship, 0.25F, std::nullopt, echo, 46.0);
        Require(staleEcho.has_value() && !staleEcho->recentEcho.has_value(),
                "old active echo evidence must age out of the presentation without mutating Tracks");

        auto lost = bearingOnlySonarTrack;
        lost.lifecycle = TrackLifecycleState::Lost;
        const std::array lostOnly{lost};
        const auto lostPresentation = DeepRun::Game::Combat::BuildSonarPresentation(
            lostOnly, 51U, ownship, 0.25F, std::nullopt, std::nullopt, 42.0);
        Require(lostPresentation.has_value() && lostPresentation->tracks.empty(),
                "Lost tracks must not remain on the player sonar scope");
    }

'''
replace_once("Tests/M5WeaponRuntimeTest.cpp", test_anchor, test_insert)

print("M5 sonar visualization patch applied")
