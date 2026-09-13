from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def replace_once(rel: str, old: str, new: str) -> None:
    p = ROOT / rel
    s = p.read_text(encoding="utf-8")
    n = s.count(old)
    if n != 1:
        raise RuntimeError(f"{rel}: expected one occurrence, found {n}: {old[:300]!r}")
    p.write_text(s.replace(old, new, 1), encoding="utf-8", newline="\n")


# Hostile visual watch: an exposed periscope mast becomes ordinary optical evidence,
# bearing-only and without player identity/classification leaking into destroyer AI.
replace_once(
    "Game/Combat/PeriscopeObservationSystem.h",
    '''[[nodiscard]] inline PeriscopePresentationSnapshot BuildPeriscopePresentationSnapshot(
''',
    '''struct ExposedPeriscopeMastDetectionConfig final
{
    // GAME POLICY for a small exposed mast observed from a surface combatant. This is deliberately much shorter
    // than the player's deliberate high-power periscope target-detection envelope and is not a historical spec.
    float maximumDetectionRangeMeters = 8'000.0F;
    float bearingUncertaintyRadians = 1.0F * PeriscopePi / 180.0F;
    float minimumConfidence = 0.58F;
};

[[nodiscard]] inline std::expected<std::optional<Perception::SensorObservation>, std::string>
ObserveExposedPeriscopeMast(
    const ExposedPeriscopeMastDetectionConfig& config,
    const PeriscopeState& state,
    const Physics::PhysicsVector3& ownshipPositionMeters,
    const float signedDepthMeters,
    const float surfaceLevelYMeters,
    const Physics::PhysicsVector3& observerPositionMeters,
    const double simulationTimeSeconds,
    const PeriscopeOpticalConditions& conditions = {})
{
    if (!std::isfinite(config.maximumDetectionRangeMeters) || config.maximumDetectionRangeMeters <= 0.0F ||
        !std::isfinite(config.bearingUncertaintyRadians) || config.bearingUncertaintyRadians < 0.0F ||
        !std::isfinite(config.minimumConfidence) || config.minimumConfidence < 0.0F || config.minimumConfidence > 1.0F ||
        !ownshipPositionMeters.IsFinite() || !observerPositionMeters.IsFinite() || !std::isfinite(signedDepthMeters) ||
        signedDepthMeters < 0.0F || !std::isfinite(surfaceLevelYMeters) || !std::isfinite(simulationTimeSeconds) ||
        simulationTimeSeconds < 0.0 || !ValidPeriscopeOpticalConditions(conditions))
    {
        return std::unexpected("invalid exposed-periscope visual detection input");
    }
    if (!state.raised || signedDepthMeters > PeriscopeObservationConfig{}.maximumOperatingDepthMeters)
        return std::optional<Perception::SensorObservation>{};

    const Physics::PhysicsVector3 mastTop{
        .x = ownshipPositionMeters.x,
        .y = surfaceLevelYMeters + PeriscopeObservationConfig{}.opticalHeadHeightAboveSurfaceMeters,
        .z = ownshipPositionMeters.z};
    const float dx = mastTop.x - observerPositionMeters.x;
    const float dy = mastTop.y - observerPositionMeters.y;
    const float distanceMeters = std::hypot(dx, dy);
    const float quality = PeriscopeDetectionQuality(conditions);
    const float effectiveRange = std::min({
        config.maximumDetectionRangeMeters * quality,
        conditions.meteorologicalVisibilityMeters,
        PeriscopeObservationConfig{}.clearDayMeteorologicalVisibilityMeters});
    if (!std::isfinite(distanceMeters) || distanceMeters > effectiveRange)
        return std::optional<Perception::SensorObservation>{};

    const float rangeFraction = std::clamp(distanceMeters / effectiveRange, 0.0F, 1.0F);
    const float confidence = std::clamp(
        (1.0F - 0.35F * rangeFraction) * quality,
        config.minimumConfidence,
        1.0F);
    return std::optional<Perception::SensorObservation>{Perception::SensorObservation{
        .modality = Perception::SensorModality::Optical,
        .sensorId = "DESTROYER_VISUAL_WATCH",
        .sensorPositionMeters = observerPositionMeters,
        .observationTimeSeconds = simulationTimeSeconds,
        .measuredBearingRadians = std::atan2(dy, dx),
        .bearingUncertaintyRadians = config.bearingUncertaintyRadians,
        .estimatedRangeMeters = std::nullopt,
        .rangeUncertaintyMeters = std::nullopt,
        .confidence = confidence,
        .opticalIdentificationLevel = Perception::OpticalIdentificationLevel::Detected,
        .classificationEvidence = std::nullopt}};
}

[[nodiscard]] inline PeriscopePresentationSnapshot BuildPeriscopePresentationSnapshot(
''')

replace_once(
    "Game/Combat/CombatPlaygroundRuntime.h",
    '''        else if (!destroyerTracks_.AdvanceTo(simulationTimeSeconds))
        {
            return std::unexpected("M5-H destroyer TrackManager failed to advance");
        }

        if (!destroyerActivePulse_ && simulationTimeSeconds >= nextDestroyerActivePulseTimeSeconds_)
''',
    '''        else if (!destroyerTracks_.AdvanceTo(simulationTimeSeconds))
        {
            return std::unexpected("M5-H destroyer TrackManager failed to advance");
        }

        // Raising the production periscope is now a real information/exposure trade-off. Hostile visual watch
        // sees only a small mast through an ordinary optical SensorObservation; it gets bearing/confidence but
        // no authoritative player body/entity identity, no range shortcut and no magical classification.
        if (periscopeState_.raised)
        {
            const float surfaceLevelYMeters =
                playerSnapshot.emitter.positionMeters.y + playerSnapshot.signedDepthMeters;
            const auto mastObserved = ObserveExposedPeriscopeMast(
                ExposedPeriscopeMastDetectionConfig{},
                periscopeState_,
                playerSnapshot.emitter.positionMeters,
                playerSnapshot.signedDepthMeters,
                surfaceLevelYMeters,
                destroyerAcoustics->passiveReceiver.positionMeters,
                simulationTimeSeconds,
                periscopeOpticalConditions_);
            if (!mastObserved)
                return std::unexpected("destroyer visual-watch periscope detection failed: " + mastObserved.error());
            if (mastObserved->has_value() && !destroyerTracks_.IntegrateObservation(**mastObserved))
                return std::unexpected("destroyer visual-watch evidence failed perception integration");
        }

        if (!destroyerActivePulse_ && simulationTimeSeconds >= nextDestroyerActivePulseTimeSeconds_)
''')

# Regression: raised mast is optically detectable, but stowed mast is not and the observation remains bearing-only.
replace_once(
    "Tests/PeriscopeBallastGameplayChecks.h",
    '''    const auto optical = ObserveThroughPeriscope(
''',
    '''    const auto exposedMast = ObserveExposedPeriscopeMast(
        ExposedPeriscopeMastDetectionConfig{},
        raisedPeriscope,
        periscopeOwnship,
        10.0F,
        0.0F,
        {.x = 3'000.0F, .y = 0.0F, .z = 0.0F},
        0.97);
    const auto stowedMast = ObserveExposedPeriscopeMast(
        ExposedPeriscopeMastDetectionConfig{},
        PeriscopeState{},
        periscopeOwnship,
        10.0F,
        0.0F,
        {.x = 3'000.0F, .y = 0.0F, .z = 0.0F},
        0.98);
    if (!exposedMast || !exposedMast->has_value() || !stowedMast || stowedMast->has_value() ||
        (*exposedMast)->modality != Perception::SensorModality::Optical ||
        (*exposedMast)->estimatedRangeMeters.has_value() || (*exposedMast)->classificationEvidence.has_value() ||
        (*exposedMast)->opticalIdentificationLevel != Perception::OpticalIdentificationLevel::Detected)
    {
        return false;
    }

    const auto optical = ObserveThroughPeriscope(
''')

# Player-facing optical view uses perceived state only. No target body/Transform enters presentation.
replace_once(
    "Game/Combat/CombatCommandUi.cpp",
    '''    ImGui::TextUnformatted("X / F            Deploy decoy");
    ImGui::End();
}

void DrawSonarScope''',
    '''    ImGui::TextUnformatted("X / F            Deploy decoy");
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

void DrawSonarScope''')

# Documentation: close stale status and explicitly record the corrective contracts.
replace_once(
    "docs/roadmap/milestones.md",
    '''## Active milestones

### Milestone 5 - Combat Playground

Status: IN PROGRESS
''',
    '''## Completed milestones (continued)

### Milestone 5 - Combat Playground

Status: COMPLETE
''')
replace_once(
    "docs/roadmap/milestones.md",
    '''- J4 player defensive acoustic decoy with reciprocal seeker diversion through perceived tracks
- J5 explicit player active-sonar ranging from a selected passive Track plus D1 combat-control convergence
- J4 controller/keyboard player defensive decoy command with reciprocal torpedo local-seeker diversion through perceived acoustics
''',
    '''- J4 player defensive acoustic decoy with reciprocal seeker diversion through perceived tracks
- J5 explicit player active-sonar ranging from a selected passive Track plus D1 combat-control convergence
- production P-700 lifecycle, launcher inventory, physical impact and visual acceptance
- low/zero-speed ballast/trim, periscope optical identification, civilian-risk ROE and exposed-mast detection
- corrective closure: deployment-only bow planes, stern-only hydrodynamic pitch, surfaced reserve buoyancy,
  production periscope animation, continuous travelling waves and non-disappearing M5 regional seabed
''')
replace_once(
    "docs/roadmap/milestones.md",
    '''- P700 runtime weapon definition and entity using the accepted C0 content asset
- missile launch and authoritative underwater-to-air transition
''',
    '''- additional missile/ASW weapon families beyond the already accepted M5 P-700 path
''')

replace_once(
    "docs/development/periscope-ballast-gameplay.md",
    '''Status: IMPLEMENTED — FINAL CI / HUMAN ACCEPTANCE PENDING
''',
    '''Status: CORRECTIVE CLOSURE CANDIDATE — FINAL CI / HUMAN ACCEPTANCE PENDING
''')
replace_once(
    "docs/development/periscope-ballast-gameplay.md",
    '''At ordinary forward speed, bow/stern diving planes remain the primary pitch/depth-control mechanism and their force continues to emerge from local water flow.
''',
    '''At ordinary forward speed, only the stern horizontal planes provide hydrodynamic pitch authority. The production bow planes are deployment-only: they are either housed or extended and never rotate with Depth input or generate control-surface force in Deep Run.
''')
replace_once(
    "docs/development/periscope-ballast-gameplay.md",
    '''- `P / D-pad Up` — raise or stow periscope;
''',
    '''- `P / D-pad Up` — raise or stow the production primary periscope;

Production mapping for this gameplay slice is `sail.retractable.03` / `SM_Antey_LOD0_SailDevice_08`. It was selected from direct production-GLB geometry inspection and the public Project 949A retractable-device arrangement as the primary gameplay periscope. `sail.retractable.09` / `SailDevice_17` is retained as the secondary periscope. These semantic mappings do not claim undocumented internal hardware characteristics. The primary mast uses the already-authored stowed/deployed transforms and a 2.5 s GAME-POLICY animation.
''')
replace_once(
    "docs/development/periscope-ballast-gameplay.md",
    '''A raised mast is exposed state. It can feed the existing signature/detection architecture as an optical-mast/periscope exposure channel when hostile visual sensing is implemented. The player therefore trades information quality for detectability.
''',
    '''A raised mast is exposed state. The destroyer's visual watch now produces an ordinary bearing-only Optical `SensorObservation` for an exposed mast inside its bounded visual envelope. The observation carries no player body/entity identity, no free range and no classification; it enters the destroyer's normal TrackManager and can therefore provoke active ranging/engagement. The player trades information quality for detectability.
''')

replace_once(
    "docs/development/m5-v2-player-navigation-sonar.md",
    '''- production bow/stern plane visuals reflect committed simulation control-surface deflection;
''',
    '''- production bow planes are deployment-only and never visually rotate; stern-plane visuals reflect committed hydrodynamic deflection;
''')

replace_once(
    "docs/development/m5-combat-playground.md",
    '''## Current accepted baseline
''',
    '''### Final corrective navigation / surface / periscope / environment closure

The post-M5 corrective pass removes the remaining human-play gaps without changing the accepted perceived-world or weapon authority boundaries. Bow planes are deployment-only throughout authoring/sidecars/runtime/HUD and only stern horizontal planes retain hydrodynamic pitch deflection. The Game-owned hydrostatic model now carries an explicit 32% reserve-buoyancy policy with submerged trim compensation, giving a natural surfaced equilibrium near three-quarters submerged instead of forcing the boat back under water. The primary production periscope (`sail.retractable.03`, production `SailDevice_08`) animates between its authored stowed/deployed transforms; an exposed mast creates ordinary bearing-only optical evidence for hostile visual watch. The player also receives a perceived-data-only periscope reticle/view. Gerstner surface presentation follows the travelling camera while preserving absolute-world phase, and known M5 regional seabed presentation no longer deepens/disappears merely because ownship moves away from the original 800 m M3 section.

## Current accepted baseline
''')

# Architecture explicitly reserves orcas too; implementation remains a later living-ocean content slice.
replace_once(
    "docs/architecture/simulation-spec.md",
    '''The architecture reserves lightweight authored behaviour for whales, sperm
whales, dolphins, fish/small schools, gulls, and pelicans.''',
    '''The architecture reserves lightweight authored behaviour for whales, sperm
whales, orcas, dolphins, fish/small schools, gulls, and pelicans.''')
replace_once(
    "docs/architecture/acoustics-spec.md",
    '''- sperm-whale clicks;
- dolphin vocalisation;
''',
    '''- sperm-whale clicks;
- orca vocalisation;
- dolphin vocalisation;
''')

print("M5 final closure phase 2 applied")
