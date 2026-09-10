from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    if old not in text:
        raise RuntimeError(f"replacement anchor not found in {path}: {old[:120]!r}")
    if text.count(old) != 1:
        raise RuntimeError(f"replacement anchor not unique in {path}: {old[:120]!r}")
    p.write_text(text.replace(old, new, 1), encoding="utf-8", newline="\n")


# Read-only commander/UI projection gains only perceived incoming-threat evidence.
replace_once(
    "Game/Combat/PlayerCombatCommandRuntime.h",
    "    bool canPrepareWeapon = false;\n    bool canFireWeapon = false;\n    std::optional<PlayerCombatCommandFeedback> lastCommand{};",
    "    bool canPrepareWeapon = false;\n    bool canFireWeapon = false;\n\n"
    "    // M5-J3 is populated by CombatPlaygroundRuntime from a dedicated passive-acoustic perceived-world path.\n"
    "    // No hostile transform, range estimate, weapon runtime pointer, or PhysicsBodyHandle is exposed to UI.\n"
    "    bool incomingThreatDetected = false;\n"
    "    std::optional<Perception::TrackLifecycleState> incomingThreatLifecycle{};\n"
    "    std::optional<float> incomingThreatBearingRadians{};\n"
    "    std::optional<float> incomingThreatBearingUncertaintyRadians{};\n"
    "    std::optional<float> incomingThreatConfidence{};\n"
    "    std::optional<PlayerCombatCommandFeedback> lastCommand{};"
)

runtime = "Game/Combat/CombatPlaygroundRuntime.h"
replace_once(
    runtime,
    "        const auto playerTorpedoSeekerTracks = Perception::TrackManager::Create(Perception::TrackManagerConfig{\n"
    "            .associationGateRadians = M5CombatTorpedoSeekerAssociationGateRadians,\n"
    "            .observationsToConfirm = 1U,\n"
    "            .coastAfterSeconds = 0.35,\n"
    "            .lostAfterSeconds = 1.5,\n"
    "            .confidenceDecayPerSecond = 0.50F,\n"
    "            .bearingUncertaintyGrowthRadiansPerSecond = 0.02F,\n"
    "            .positionUncertaintyGrowthMetersPerSecond = 0.0F,\n"
    "            .maximumTracks = 8U});\n"
    "        if (!playerTracks || !destroyerTracks || !playerTorpedoSeekerTracks)\n"
    "        {\n"
    "            return std::unexpected(\"M5-H/M5-E.1 perception manager creation failed\");\n"
    "        }",
    "        const auto playerTorpedoSeekerTracks = Perception::TrackManager::Create(Perception::TrackManagerConfig{\n"
    "            .associationGateRadians = M5CombatTorpedoSeekerAssociationGateRadians,\n"
    "            .observationsToConfirm = 1U,\n"
    "            .coastAfterSeconds = 0.35,\n"
    "            .lostAfterSeconds = 1.5,\n"
    "            .confidenceDecayPerSecond = 0.50F,\n"
    "            .bearingUncertaintyGrowthRadiansPerSecond = 0.02F,\n"
    "            .positionUncertaintyGrowthMetersPerSecond = 0.0F,\n"
    "            .maximumTracks = 8U});\n"
    "        const auto incomingThreatTracks = Perception::TrackManager::Create(Perception::TrackManagerConfig{\n"
    "            .associationGateRadians = 0.12F,\n"
    "            .observationsToConfirm = 2U,\n"
    "            .coastAfterSeconds = 0.75,\n"
    "            .lostAfterSeconds = 2.0,\n"
    "            .confidenceDecayPerSecond = 0.40F,\n"
    "            .bearingUncertaintyGrowthRadiansPerSecond = 0.03F,\n"
    "            .positionUncertaintyGrowthMetersPerSecond = 0.0F,\n"
    "            .maximumTracks = 4U});\n"
    "        if (!playerTracks || !destroyerTracks || !playerTorpedoSeekerTracks || !incomingThreatTracks)\n"
    "        {\n"
    "            return std::unexpected(\"M5-H/M5-E.1/M5-J3 perception manager creation failed\");\n"
    "        }"
)
replace_once(
    runtime,
    "            *destroyerTracks,\n            *playerTorpedoSeekerTracks,\n            destroyerDefinition,",
    "            *destroyerTracks,\n            *playerTorpedoSeekerTracks,\n            *incomingThreatTracks,\n            destroyerDefinition,"
)
replace_once(
    runtime,
    "        Perception::TrackManager destroyerTracks,\n        Perception::TrackManager playerTorpedoSeekerTracks,\n        SimpleDestroyerDefinition destroyerDefinition,",
    "        Perception::TrackManager destroyerTracks,\n        Perception::TrackManager playerTorpedoSeekerTracks,\n        Perception::TrackManager incomingThreatTracks,\n        SimpleDestroyerDefinition destroyerDefinition,"
)
replace_once(
    runtime,
    "          destroyerTracks_(std::move(destroyerTracks)),\n          playerTorpedoSeekerTracks_(std::move(playerTorpedoSeekerTracks)),\n          destroyerDefinition_(std::move(destroyerDefinition)),",
    "          destroyerTracks_(std::move(destroyerTracks)),\n          playerTorpedoSeekerTracks_(std::move(playerTorpedoSeekerTracks)),\n          incomingThreatTracks_(std::move(incomingThreatTracks)),\n          destroyerDefinition_(std::move(destroyerDefinition)),"
)
replace_once(
    runtime,
    "        std::optional<Weapons::ConventionalTorpedoImpact> destroyerImpact{};\n        if (destroyerTorpedo_ && destroyerTorpedo_->movementDomain == Weapons::MovementDomain::Underwater)",
    "        // M5-J3 warning evidence is produced through the same AcousticWorld -> SensorObservation -> TrackManager\n"
    "        // boundary as other perceived-world data. The simulator may know the hostile torpedo position to emit\n"
    "        // sound, but the commander projection receives bearing/confidence only.\n"
    "        const auto threatPerception = AdvanceIncomingThreatPerception(playerSnapshot, simulationTimeSeconds);\n"
    "        if (!threatPerception)\n"
    "        {\n"
    "            return std::unexpected(threatPerception.error());\n"
    "        }\n\n"
    "        std::optional<Weapons::ConventionalTorpedoImpact> destroyerImpact{};\n"
    "        if (destroyerTorpedo_ && destroyerTorpedo_->movementDomain == Weapons::MovementDomain::Underwater)"
)
replace_once(
    runtime,
    "        return CombatPlaygroundFrame{\n            .playerTracks = playerTrackSnapshot,\n            .destroyerTracks = destroyerTracks_.Tracks(),\n            .playerCombat = playerCombat_.BuildPresentationSnapshot(playerTrackSnapshot),",
    "        PlayerCombatPresentationSnapshot playerCombatPresentation =\n"
    "            playerCombat_.BuildPresentationSnapshot(playerTrackSnapshot);\n"
    "        ApplyIncomingThreatPresentation(playerCombatPresentation);\n"
    "        return CombatPlaygroundFrame{\n"
    "            .playerTracks = playerTrackSnapshot,\n"
    "            .destroyerTracks = destroyerTracks_.Tracks(),\n"
    "            .playerCombat = std::move(playerCombatPresentation),"
)
replace_once(
    runtime,
    "    [[nodiscard]] static double Distance(\n",
    "    [[nodiscard]] std::expected<void, std::string> AdvanceIncomingThreatPerception(\n"
    "        const Submarine::AnteyAcousticSnapshot& playerSnapshot,\n"
    "        const double simulationTimeSeconds)\n"
    "    {\n"
    "        if (!playerSnapshot.passiveReceiver.positionMeters.IsFinite() ||\n"
    "            !std::isfinite(simulationTimeSeconds))\n"
    "        {\n"
    "            return std::unexpected(\"M5-J3 incoming-threat receiver/time input is invalid\");\n"
    "        }\n\n"
    "        bool integratedObservation = false;\n"
    "        if (destroyerTorpedo_ && destroyerTorpedo_->movementDomain == Weapons::MovementDomain::Underwater &&\n"
    "            destroyerTorpedo_->positionMeters.IsFinite())\n"
    "        {\n"
    "            const double distanceMeters = Distance(\n"
    "                destroyerTorpedo_->positionMeters, playerSnapshot.passiveReceiver.positionMeters);\n"
    "            const double travelSeconds = distanceMeters /\n"
    "                static_cast<double>(acousticWorld_.Config().effectiveSoundSpeedMetersPerSecond);\n"
    "            if (!std::isfinite(travelSeconds))\n"
    "            {\n"
    "                return std::unexpected(\"M5-J3 incoming-threat acoustic travel time is invalid\");\n"
    "            }\n"
    "            const double emissionTimeSeconds = simulationTimeSeconds > travelSeconds\n"
    "                ? std::max(0.0, simulationTimeSeconds - travelSeconds - 1.0e-6)\n"
    "                : 0.0;\n"
    "            const Acoustics::AcousticEmission emission{\n"
    "                .positionMeters = destroyerTorpedo_->positionMeters,\n"
    "                // Gameplay-authored coarse machinery/propulsor signature for the M5 warning slice.\n"
    "                .sourceLevelDb = {.levelDb = {176.0F, 172.0F, 164.0F, 156.0F}},\n"
    "                .emissionTimeSeconds = emissionTimeSeconds};\n"
    "            const auto observed = acousticWorld_.CollectPassiveDirectObservation(\n"
    "                emission, playerSnapshot.passiveReceiver, simulationTimeSeconds);\n"
    "            if (!observed)\n"
    "            {\n"
    "                return std::unexpected(\"M5-J3 incoming-threat acoustic propagation failed: \" +\n"
    "                                       observed.error().message);\n"
    "            }\n"
    "            if (observed->has_value())\n"
    "            {\n"
    "                const auto perceived = Perception::FromAcousticObservation(**observed);\n"
    "                if (!perceived || !incomingThreatTracks_.IntegrateObservation(*perceived))\n"
    "                {\n"
    "                    return std::unexpected(\"M5-J3 incoming-threat evidence failed perception integration\");\n"
    "                }\n"
    "                integratedObservation = true;\n"
    "            }\n"
    "        }\n\n"
    "        if (!integratedObservation && !incomingThreatTracks_.AdvanceTo(simulationTimeSeconds))\n"
    "        {\n"
    "            return std::unexpected(\"M5-J3 incoming-threat TrackManager failed to advance\");\n"
    "        }\n"
    "        return {};\n"
    "    }\n\n"
    "    void ApplyIncomingThreatPresentation(PlayerCombatPresentationSnapshot& snapshot) const noexcept\n"
    "    {\n"
    "        const Perception::Track* best = nullptr;\n"
    "        for (const auto& track : incomingThreatTracks_.Tracks())\n"
    "        {\n"
    "            const bool present = track.lifecycle == Perception::TrackLifecycleState::Confirmed ||\n"
    "                                 track.lifecycle == Perception::TrackLifecycleState::Coasting;\n"
    "            if (!present)\n"
    "            {\n"
    "                continue;\n"
    "            }\n"
    "            if (best == nullptr || track.confidence > best->confidence ||\n"
    "                (track.confidence == best->confidence && track.trackId < best->trackId))\n"
    "            {\n"
    "                best = &track;\n"
    "            }\n"
    "        }\n"
    "        if (best == nullptr)\n"
    "        {\n"
    "            return;\n"
    "        }\n\n"
    "        snapshot.incomingThreatDetected = true;\n"
    "        snapshot.incomingThreatLifecycle = best->lifecycle;\n"
    "        snapshot.incomingThreatBearingRadians = best->estimatedBearingRadians;\n"
    "        snapshot.incomingThreatBearingUncertaintyRadians = best->bearingUncertaintyRadians;\n"
    "        snapshot.incomingThreatConfidence = best->confidence;\n"
    "    }\n\n"
    "    [[nodiscard]] static double Distance(\n"
)
replace_once(
    runtime,
    "    Perception::TrackManager destroyerTracks_;\n    Perception::TrackManager playerTorpedoSeekerTracks_;",
    "    Perception::TrackManager destroyerTracks_;\n    Perception::TrackManager playerTorpedoSeekerTracks_;\n    Perception::TrackManager incomingThreatTracks_;"
)

# UI displays only the perceived warning projection.
replace_once(
    "Game/Combat/CombatCommandUi.cpp",
    "    ImGui::Separator();\n    ImGui::Text(\"Prepare available: %s\", snapshot.canPrepareWeapon ? \"YES\" : \"NO\");",
    "    ImGui::Separator();\n"
    "    ImGui::TextUnformatted(\"THREAT\");\n"
    "    if (!snapshot.incomingThreatDetected)\n"
    "    {\n"
    "        ImGui::TextUnformatted(\"Incoming acoustic threat: NONE\");\n"
    "    }\n"
    "    else\n"
    "    {\n"
    "        ImGui::TextUnformatted(\"Incoming acoustic threat: DETECTED\");\n"
    "        if (snapshot.incomingThreatLifecycle)\n"
    "        {\n"
    "            ImGui::Text(\"Threat track: %s\", TrackLifecycleName(*snapshot.incomingThreatLifecycle));\n"
    "        }\n"
    "        if (snapshot.incomingThreatBearingRadians)\n"
    "        {\n"
    "            constexpr float radiansToDegrees = 57.2957795F;\n"
    "            ImGui::Text(\"Threat bearing: %.1f deg\", *snapshot.incomingThreatBearingRadians * radiansToDegrees);\n"
    "        }\n"
    "        if (snapshot.incomingThreatBearingUncertaintyRadians)\n"
    "        {\n"
    "            constexpr float radiansToDegrees = 57.2957795F;\n"
    "            ImGui::Text(\"Threat bearing uncertainty: +/- %.1f deg\",\n"
    "                        *snapshot.incomingThreatBearingUncertaintyRadians * radiansToDegrees);\n"
    "        }\n"
    "        if (snapshot.incomingThreatConfidence)\n"
    "        {\n"
    "            ImGui::Text(\"Threat confidence: %.0f%%\", *snapshot.incomingThreatConfidence * 100.0F);\n"
    "        }\n"
    "    }\n\n"
    "    ImGui::Separator();\n"
    "    ImGui::Text(\"Prepare available: %s\", snapshot.canPrepareWeapon ? \"YES\" : \"NO\");"
)

# Runtime regression proves no warning before the hostile weapon exists, bounded perceived fields after detection,
# and natural TrackManager clearing after physical impact.
test = "Tests/M5CombatPlaygroundRuntimeChecks.h"
replace_once(
    test,
    "    bool sawDestroyerTorpedoHiddenAfterImpact = false;\n    bool sawTorpedo = false;",
    "    bool sawDestroyerTorpedoHiddenAfterImpact = false;\n"
    "    bool sawIncomingThreat = false;\n"
    "    bool sawIncomingThreatClearedAfterImpact = false;\n"
    "    bool sawTorpedo = false;"
)
replace_once(
    test,
    "        sawDestroyerPreparation = sawDestroyerPreparation ||\n            frame->destroyerDecision.action == Game::Combat::SimpleDestroyerCombatAction::PrepareWeapon;",
    "        if (frame->playerCombat.incomingThreatDetected)\n"
    "        {\n"
    "            if (!frame->playerCombat.incomingThreatLifecycle ||\n"
    "                !frame->playerCombat.incomingThreatBearingRadians ||\n"
    "                !frame->playerCombat.incomingThreatBearingUncertaintyRadians ||\n"
    "                !frame->playerCombat.incomingThreatConfidence ||\n"
    "                !std::isfinite(*frame->playerCombat.incomingThreatBearingRadians) ||\n"
    "                !std::isfinite(*frame->playerCombat.incomingThreatBearingUncertaintyRadians) ||\n"
    "                *frame->playerCombat.incomingThreatBearingUncertaintyRadians <= 0.0F ||\n"
    "                !std::isfinite(*frame->playerCombat.incomingThreatConfidence) ||\n"
    "                *frame->playerCombat.incomingThreatConfidence < 0.0F ||\n"
    "                *frame->playerCombat.incomingThreatConfidence > 1.0F)\n"
    "            {\n"
    "                return false;\n"
    "            }\n"
    "            sawIncomingThreat = true;\n"
    "        }\n"
    "        else\n"
    "        {\n"
    "            if (frame->playerCombat.incomingThreatLifecycle ||\n"
    "                frame->playerCombat.incomingThreatBearingRadians ||\n"
    "                frame->playerCombat.incomingThreatBearingUncertaintyRadians ||\n"
    "                frame->playerCombat.incomingThreatConfidence)\n"
    "            {\n"
    "                return false;\n"
    "            }\n"
    "            if (!sawDestroyerTorpedoMaterialized && sawIncomingThreat)\n"
    "            {\n"
    "                return false;\n"
    "            }\n"
    "            if (sawDestroyerTorpedoImpact)\n"
    "            {\n"
    "                sawIncomingThreatClearedAfterImpact = true;\n"
    "            }\n"
    "        }\n\n"
    "        sawDestroyerPreparation = sawDestroyerPreparation ||\n"
    "            frame->destroyerDecision.action == Game::Combat::SimpleDestroyerCombatAction::PrepareWeapon;"
)
# Explicit pre-materialization leak guard at the top of the loop.
replace_once(
    test,
    "        const auto cameraFraming = cameraDirector.Evaluate(runtime, simulationTimeSeconds);",
    "        if (!runtime.DestroyerTorpedo() && frame->playerCombat.incomingThreatDetected)\n"
    "        {\n"
    "            return false;\n"
    "        }\n\n"
    "        const auto cameraFraming = cameraDirector.Evaluate(runtime, simulationTimeSeconds);"
)
replace_once(
    test,
    "        !sawDestroyerTorpedoMaterialized || !sawDestroyerTorpedoImpact ||\n        !sawDestroyerTorpedoUnderwaterWithoutBodyIdentity || !sawDestroyerTorpedoHiddenAfterImpact || !sawTorpedo ||",
    "        !sawDestroyerTorpedoMaterialized || !sawDestroyerTorpedoImpact ||\n"
    "        !sawDestroyerTorpedoUnderwaterWithoutBodyIdentity || !sawDestroyerTorpedoHiddenAfterImpact ||\n"
    "        !sawIncomingThreat || !sawIncomingThreatClearedAfterImpact || !sawTorpedo ||"
)
replace_once(
    test,
    "    if (!finalPresentation || std::abs(finalPresentation->destroyerIntegrityFraction - 0.40F) > 0.001F)\n    {\n        return false;\n    }",
    "    if (!finalPresentation || std::abs(finalPresentation->destroyerIntegrityFraction - 0.40F) > 0.001F)\n"
    "    {\n"
    "        return false;\n"
    "    }\n"
    "    const auto finalCommanderProjection = runtime.PlayerCombat().BuildPresentationSnapshot(runtime.PlayerTorpedo()\n"
    "        ? std::span<const Perception::Track>{}\n"
    "        : std::span<const Perception::Track>{});\n"
    "    (void)finalCommanderProjection;"
)
# Remove the intentionally no-op local projection above; the frame-level final clearing is already proven in-loop.
replace_once(
    test,
    "    const auto finalCommanderProjection = runtime.PlayerCombat().BuildPresentationSnapshot(runtime.PlayerTorpedo()\n"
    "        ? std::span<const Perception::Track>{}\n"
    "        : std::span<const Perception::Track>{});\n"
    "    (void)finalCommanderProjection;",
    ""
)

# Bring project status documentation up to the accepted runtime baseline and record J3's bounded contract.
replace_once(
    "docs/roadmap/milestones.md",
    "## Future milestones\n\n### Milestone 5 - Combat Playground\n\nFuture work:\n\n- destroyer\n- first conventional heavyweight torpedo\n- decoy\n- mine\n- explosions\n- basic damage\n- simple combat AI using observations/contacts/tracks rather than player ground\n  truth",
    "## Active milestones\n\n### Milestone 5 - Combat Playground\n\nStatus: IN PROGRESS\n\nAccepted implementation now includes:\n\n- track-constrained weapon readiness and commander sequencing\n- active ranging and spatial perceived tracks with uncertainty\n- conventional heavyweight torpedo movement, seeker guidance and swept physical impact\n- acoustic decoy diversion/recovery through the ordinary perceived-world pipeline\n- live naval mine hazard bound to production Antey collision authority\n- simple destroyer Track-only combat AI, active fire-control ranging and reciprocal torpedo attack\n- bounded combat integrity, explosions, multi-scale presentation and controller-first combat commands\n- J3 passive-acoustic incoming-threat warning projected to combat UI without hostile position/body truth"
)
replace_once(
    "docs/roadmap/milestones.md",
    "<!-- deeprun-m5-command-note:end -->\n\n### Milestone 6 - Submarine Systems",
    "<!-- deeprun-m5-command-note:end -->\n\n## Future milestones\n\n### Milestone 6 - Submarine Systems"
)
replace_once(
    "docs/development/m5-combat-playground.md",
    "Status: IN PROGRESS\n\n## M5-H.1-B — automated windowed visual acceptance",
    "Status: IN PROGRESS\n\n"
    "## Current accepted baseline\n\n"
    "The stable M5 branch now includes the A-D weapon/perception/impact foundation; live decoy seeker integration;\n"
    "simple destroyer AI with active fire-control ranging; production-bound live naval mine collision; J1/J2\n"
    "controller-first commander commands; H/H.4 combat/environment presentation; and F.2 reciprocal destroyer\n"
    "torpedo threat. F.2 was stabilized by extending only the deterministic headless engagement horizon from\n"
    "45 s to 80 s so the legitimately delayed F.1 ranged fire-control launch has enough SimulationTime to traverse\n"
    "the ~1.7 km scenario. Gameplay speed/guidance/damage were unchanged. Stable evidence: clean candidate SHA\n"
    "`3fd4cad9faf9d89787348523dfcb4e20ed1aaac9` passed Debug and Release twice, and feature commit\n"
    "`1d90159ac236cc3cc2df0a1d07b6fbf4c4d5fdc1` passed post-merge CI run `34520094788` in both configurations.\n\n"
    "### M5-J3 — perceived incoming-threat combat UI\n\n"
    "Status: ACCEPTED when this documented tree passes the normal Debug/Release CI gate.\n\n"
    "J3 does not expose hostile torpedo position, range, Transform or PhysicsBodyHandle to commander UI. A dedicated\n"
    "passive-acoustic perceived-world path samples the simulated hostile weapon only as an AcousticEmission, feeds\n"
    "the production player passive receiver through AcousticWorld, converts observations through SensorObservation\n"
    "and TrackManager, and projects only lifecycle, bearing, bearing uncertainty and confidence. The warning naturally\n"
    "coasts/clears after the physical threat is consumed. No tactical pause or generic command queue is introduced.\n\n"
    "## M5-H.1-B — automated windowed visual acceptance"
)
replace_once(
    "docs/development/m5-combat-playground.md",
    "## M5-D — swept collision, impact, explosion event and bounded combat damage\n\nStatus: IMPLEMENTED — acceptance pending CI.",
    "## M5-D — swept collision, impact, explosion event and bounded combat damage\n\nStatus: ACCEPTED."
)

print("M5-J3 incoming threat integration patch applied")
