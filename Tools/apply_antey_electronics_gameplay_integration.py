from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def replace_once(path: Path, old: str, new: str) -> None:
    text = path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected one marker, found {count}: {old[:120]!r}")
    path.write_text(text.replace(old, new), encoding="utf-8")


def patch_electronic_suite() -> None:
    path = ROOT / "Game/Combat/AnteyElectronicSuite.h"
    role_helper = '''[[nodiscard]] inline constexpr std::string_view AnteyElectronicSystemProductionRoleId(\n    const AnteyElectronicSystem system) noexcept\n{\n    switch (system)\n    {\n    case AnteyElectronicSystem::SynthesisSatNav: return "SYNTHESIS_SATNAV";\n    case AnteyElectronicSystem::ZonaRadioDirectionFinder: return "ZONA_RDF_ESM";\n    case AnteyElectronicSystem::AnisRadio: return "ANIS_RADIO";\n    case AnteyElectronicSystem::Mrsc2TargetingReceiver: return "MRSC2_TARGETING";\n    case AnteyElectronicSystem::RadianSurfaceRadar: return "RADIAN_SURFACE_RADAR";\n    case AnteyElectronicSystem::KoraCommunications: return "KORA_MOLNIYA_M";\n    case AnteyElectronicSystem::RkpCompressorIntake: return "RKP_COMPRESSOR_INTAKE";\n    case AnteyElectronicSystem::SelenaSatelliteTargeting: return "SELENA_KORALL";\n    case AnteyElectronicSystem::Signal3NavigationPeriscope: return "SIGNAL3_NAV_PERISCOPE";\n    case AnteyElectronicSystem::Pzns10AttackPeriscope: return "PZNS10S_ATTACK_PERISCOPE";\n    case AnteyElectronicSystem::Count: break;\n    }\n    return "UNKNOWN";\n}\n\n'''
    replace_once(path, "enum class ExternalTargetReportSource\n{", role_helper + "enum class ExternalTargetReportSource\n{")
    text = path.read_text(encoding="utf-8")
    # Win32 headers can define max as a macro in the main executable TU. Parenthesized calls remain standards-compliant.
    text = text.replace("std::max(", "(std::max)(")
    path.write_text(text, encoding="utf-8")


def patch_player_combat_contract() -> None:
    path = ROOT / "Game/Combat/PlayerCombatCommandRuntime.h"
    replace_once(path,
        '#include "Game/Combat/SonarPresentation.h"\n',
        '#include "Game/Combat/AnteyElectronicSuite.h"\n#include "Game/Combat/SonarPresentation.h"\n')
    replace_once(path,
        "    VisualIdentify,\n    ToggleP700SalvoMode,\n};",
        "    VisualIdentify,\n    ToggleP700SalvoMode,\n    CycleElectronicSuite,\n    OperateElectronicSuite,\n};")
    replace_once(path,
        "    SonarPresentationSnapshot sonar{};\n\n    // Periscope state",
        "    SonarPresentationSnapshot sonar{};\n\n"
        "    // Multi-sensor / retractable-device suite. These are perceived/gameplay projections, never raw truth.\n"
        "    AnteyElectronicSystem selectedElectronicSystem = AnteyElectronicSystem::ZonaRadioDirectionFinder;\n"
        "    std::array<bool, AnteyElectronicSystemCount> electronicSystemDeployed{};\n"
        "    bool radianTransmitting = false;\n"
        "    bool radioTransmitting = false;\n"
        "    bool rkpCompressorRequested = false;\n"
        "    bool rkpCompressorRunning = false;\n"
        "    float navigationErrorMeters = 0.0F;\n"
        "    float highPressureAirFraction = 1.0F;\n"
        "    std::optional<ExternalTargetReportSource> externalTargetReportSource{};\n"
        "    std::optional<float> externalTargetReportAgeSeconds{};\n"
        "    std::optional<float> externalTargetReportUncertaintyMeters{};\n"
        "    bool selectedTrackHasPassiveAcousticEvidence = false;\n"
        "    bool selectedTrackHasActiveAcousticEvidence = false;\n"
        "    bool selectedTrackHasOpticalEvidence = false;\n"
        "    bool selectedTrackHasElectronicSupportEvidence = false;\n"
        "    bool selectedTrackHasSurfaceRadarEvidence = false;\n"
        "    bool selectedTrackHasExternalReportEvidence = false;\n"
        "    std::optional<float> selectedTrackExternalReportAgeSeconds{};\n\n"
        "    // Periscope state")
    replace_once(path,
        "        case PlayerCombatCommandType::ToggleP700SalvoMode:\n            return ToggleP700SalvoMode();\n        }",
        "        case PlayerCombatCommandType::ToggleP700SalvoMode:\n            return ToggleP700SalvoMode();\n"
        "        case PlayerCombatCommandType::CycleElectronicSuite:\n"
        "        case PlayerCombatCommandType::OperateElectronicSuite:\n"
        "            return std::unexpected(\"Antey electronic-suite commands are owned by CombatPlaygroundRuntime\");\n"
        "        }")
    replace_once(path,
        "        snapshot.selectedTrackVisuallyIdentified = selected->visuallyIdentified;\n        snapshot.selectedTrackWeaponQualified",
        "        snapshot.selectedTrackVisuallyIdentified = selected->visuallyIdentified;\n"
        "        snapshot.selectedTrackHasPassiveAcousticEvidence = selected->hasPassiveAcousticEvidence;\n"
        "        snapshot.selectedTrackHasActiveAcousticEvidence = selected->hasActiveAcousticEvidence;\n"
        "        snapshot.selectedTrackHasOpticalEvidence = selected->hasOpticalEvidence;\n"
        "        snapshot.selectedTrackHasElectronicSupportEvidence = selected->hasElectronicSupportEvidence;\n"
        "        snapshot.selectedTrackHasSurfaceRadarEvidence = selected->hasSurfaceRadarEvidence;\n"
        "        snapshot.selectedTrackHasExternalReportEvidence = selected->hasExternalReportEvidence;\n"
        "        snapshot.selectedTrackExternalReportAgeSeconds = selected->externalReportAgeSeconds;\n"
        "        snapshot.selectedTrackWeaponQualified")


def patch_input() -> None:
    path = ROOT / "Engine/Input/InputState.h"
    replace_once(path,
        "    ToggleP700SalvoMode,\n    Count,",
        "    ToggleP700SalvoMode,\n    CycleElectronicSuite,\n    OperateElectronicSuite,\n    Count,")

    path = ROOT / "Engine/Input/InputSystem.h"
    replace_once(path,
        "    bool toggleP700SalvoMode = false;\n};",
        "    bool toggleP700SalvoMode = false;\n    bool cycleElectronicSuite = false;\n    bool operateElectronicSuite = false;\n};")
    replace_once(path,
        "    bool toggleP700SalvoModeKeyDown_ = false;\n};",
        "    bool toggleP700SalvoModeKeyDown_ = false;\n    bool cycleElectronicSuiteKeyDown_ = false;\n    bool operateElectronicSuiteKeyDown_ = false;\n};")

    path = ROOT / "Engine/Input/InputSystem.cpp"
    replace_once(path,
        "        .visualIdentify = HasGamepadButton(gamepad, GamepadButton::A),\n        .toggleP700SalvoMode = HasGamepadButton(gamepad, GamepadButton::DpadDown)};",
        "        .visualIdentify = HasGamepadButton(gamepad, GamepadButton::A),\n"
        "        .toggleP700SalvoMode = HasGamepadButton(gamepad, GamepadButton::DpadDown),\n"
        "        .cycleElectronicSuite = HasGamepadButton(gamepad, GamepadButton::LeftShoulder),\n"
        "        .operateElectronicSuite = HasGamepadButton(gamepad, GamepadButton::B)};")
    replace_once(path,
        "            else if (event.key == Platform::Key::G)\n            {\n                toggleP700SalvoModeKeyDown_ = true;\n            }",
        "            else if (event.key == Platform::Key::G)\n            {\n                toggleP700SalvoModeKeyDown_ = true;\n            }\n"
        "            else if (event.key == Platform::Key::R)\n            {\n                cycleElectronicSuiteKeyDown_ = true;\n            }\n"
        "            else if (event.key == Platform::Key::Equals)\n            {\n                operateElectronicSuiteKeyDown_ = true;\n            }")
    replace_once(path,
        "            else if (event.key == Platform::Key::G)\n            {\n                toggleP700SalvoModeKeyDown_ = false;\n            }",
        "            else if (event.key == Platform::Key::G)\n            {\n                toggleP700SalvoModeKeyDown_ = false;\n            }\n"
        "            else if (event.key == Platform::Key::R)\n            {\n                cycleElectronicSuiteKeyDown_ = false;\n            }\n"
        "            else if (event.key == Platform::Key::Equals)\n            {\n                operateElectronicSuiteKeyDown_ = false;\n            }")
    replace_once(path,
        "    state_.SetActionDown(InputAction::ToggleP700SalvoMode, toggleP700SalvoModeKeyDown_ || controller.toggleP700SalvoMode);\n}",
        "    state_.SetActionDown(InputAction::ToggleP700SalvoMode, toggleP700SalvoModeKeyDown_ || controller.toggleP700SalvoMode);\n"
        "    state_.SetActionDown(InputAction::CycleElectronicSuite, cycleElectronicSuiteKeyDown_ || controller.cycleElectronicSuite);\n"
        "    state_.SetActionDown(InputAction::OperateElectronicSuite, operateElectronicSuiteKeyDown_ || controller.operateElectronicSuite);\n"
        "}")


def patch_combat_runtime() -> None:
    path = ROOT / "Game/Combat/CombatPlaygroundRuntime.h"
    replace_once(path,
        '#include "Game/Combat/CombatKnowledge.h"\n',
        '#include "Game/Combat/AnteyElectronicCombatRuntime.h"\n#include "Game/Combat/CombatKnowledge.h"\n')

    # Preserve acoustic immediacy while allowing stale external-CU tracks to remain useful long enough to matter.
    replace_once(path,
        "            .observationsToConfirm = 1U,\n            .coastAfterSeconds = 5.0,\n            .lostAfterSeconds = 20.0,\n            .confidenceDecayPerSecond = 0.02F,\n            .bearingUncertaintyGrowthRadiansPerSecond = 0.01F,\n            .positionUncertaintyGrowthMetersPerSecond = 5.0F,\n            .maximumTracks = 8U});\n        const auto destroyerTracks",
        "            .observationsToConfirm = 1U,\n            .coastAfterSeconds = 30.0,\n            .lostAfterSeconds = 90.0,\n            .confidenceDecayPerSecond = 0.005F,\n            .bearingUncertaintyGrowthRadiansPerSecond = 0.005F,\n            .positionUncertaintyGrowthMetersPerSecond = 5.0F,\n            .maximumTracks = 8U});\n        const auto destroyerTracks")

    # P-700 may be launched from an external target designation with kilometer-scale uncertainty; seeker logic
    # remains responsible for terminal refinement. Torpedo profiles keep their stricter fire-control requirements.
    replace_once(path,
        "                    .minimumTrackConfidence = 0.65F,\n                    .maximumBearingUncertaintyRadians = 0.12F,\n                    .maximumPositionUncertaintyMeters = 500.0F,",
        "                    .minimumTrackConfidence = 0.65F,\n                    .maximumBearingUncertaintyRadians = 0.20F,\n                    .maximumPositionUncertaintyMeters = 20'000.0F,")

    replace_once(path,
        "          lastUpdateTimeSeconds_(simulationTimeSeconds),\n          p700AcceptanceMode_(p700AcceptanceMode)",
        "          lastUpdateTimeSeconds_(simulationTimeSeconds),\n          electronics_(simulationTimeSeconds),\n          p700AcceptanceMode_(p700AcceptanceMode)")

    # Player electronic observations are integrated immediately after the surface truth snapshot. This gives
    # external/radar/ESM data the same perceived-world fusion path as sonar and allows hostile ESM to react to TX.
    replace_once(path,
        "        const std::vector<SurfaceContactSensorTruth> surfaceTruths =\n            BuildSurfaceContactTruths(*destroyerAcoustics, civilianEmitter);\n\n        const double passiveDistance",
        "        const std::vector<SurfaceContactSensorTruth> surfaceTruths =\n            BuildSurfaceContactTruths(*destroyerAcoustics, civilianEmitter);\n\n"
        "        const auto electronicFrame = electronics_.Advance(\n"
        "            playerSnapshot, surfaceTruths, playerTracks_, destroyerTracks_,\n"
        "            destroyerAcoustics->passiveReceiver.positionMeters, simulationTimeSeconds);\n"
        "        if (!electronicFrame)\n"
        "            return std::unexpected(\"Antey electronic combat advance failed: \" + electronicFrame.error());\n"
        "        bool integratedPlayerEvidence = electronicFrame->integratedPlayerEvidence;\n\n"
        "        const double passiveDistance")

    # Any raised auxiliary mast is a visual exposure source, not only the attack periscope.
    replace_once(path,
        "        if (!destroyerActivePulse_ && simulationTimeSeconds >= nextDestroyerActivePulseTimeSeconds_)",
        "        if (electronics_.AnyAuxiliaryMastDeployed() && !periscopeState_.raised)\n"
        "        {\n"
        "            const float surfaceLevelYMeters =\n"
        "                playerSnapshot.emitter.positionMeters.y + playerSnapshot.signedDepthMeters;\n"
        "            const PeriscopeState exposedAuxiliaryMast{.raised = true, .viewBearingRadians = 0.0F};\n"
        "            const auto mastObserved = ObserveExposedPeriscopeMast(\n"
        "                ExposedPeriscopeMastDetectionConfig{}, exposedAuxiliaryMast,\n"
        "                playerSnapshot.emitter.positionMeters, playerSnapshot.signedDepthMeters,\n"
        "                surfaceLevelYMeters, destroyerAcoustics->passiveReceiver.positionMeters,\n"
        "                simulationTimeSeconds, periscopeOpticalConditions_);\n"
        "            if (!mastObserved)\n"
        "                return std::unexpected(\"destroyer visual-watch auxiliary mast detection failed: \" + mastObserved.error());\n"
        "            if (mastObserved->has_value() && !destroyerTracks_.IntegrateObservation(**mastObserved))\n"
        "                return std::unexpected(\"auxiliary mast visual exposure failed hostile Track integration\");\n"
        "        }\n\n"
        "        if (!destroyerActivePulse_ && simulationTimeSeconds >= nextDestroyerActivePulseTimeSeconds_)")

    replace_once(path,
        "        bool integratedPlayerEvidence = false;\n        const auto destroyerPassive = IntegratePlayerPassiveEmitter(",
        "        const auto destroyerPassive = IntegratePlayerPassiveEmitter(")
    replace_once(path,
        "        integratedPlayerEvidence = *destroyerPassive;",
        "        integratedPlayerEvidence = integratedPlayerEvidence || *destroyerPassive;")

    # Electronic-suite commands precede weapon selection so they never fall into the weapon runtime switch.
    replace_once(path,
        "            for (const PlayerCombatCommand command : commands)\n            {\n                if (command.type == PlayerCombatCommandType::PreviousWeapon ||",
        "            for (const PlayerCombatCommand command : commands)\n            {\n"
        "                if (command.type == PlayerCombatCommandType::CycleElectronicSuite)\n"
        "                {\n"
        "                    electronics_.CycleSelectedSystem();\n"
        "                    lastCombatCommand_ = PlayerCombatCommandFeedback{\n"
        "                        .command = command.type, .accepted = true, .trackId = playerCombat_.SelectedTrackId(),\n"
        "                        .message = \"electronic suite selected: \" +\n"
        "                            std::string(AnteyElectronicSystemName(electronics_.SelectedSystem()))};\n"
        "                    continue;\n"
        "                }\n"
        "                if (command.type == PlayerCombatCommandType::OperateElectronicSuite)\n"
        "                {\n"
        "                    if (electronics_.SelectedSystem() == AnteyElectronicSystem::Pzns10AttackPeriscope)\n"
        "                    {\n"
        "                        auto feedback = TogglePeriscopeForSelectedTrack(\n"
        "                            periscopeState_, playerTracks_, playerCombat_.SelectedTrackId(),\n"
        "                            playerSnapshot.signedDepthMeters);\n"
        "                        feedback.command = command.type;\n"
        "                        feedback.message = \"PZNS-10S / \" + feedback.message;\n"
        "                        lastCombatCommand_ = std::move(feedback);\n"
        "                        continue;\n"
        "                    }\n"
        "                    const bool mastAvailable = AnteyElectronicMastsAvailable(\n"
        "                        electronics_.Config(), playerSnapshot.signedDepthMeters);\n"
        "                    const auto operated = electronics_.OperateSelectedSystem(\n"
        "                        playerSnapshot.signedDepthMeters, simulationTimeSeconds);\n"
        "                    if (!operated)\n"
        "                        return std::unexpected(\"electronic-suite command failed: \" + operated.error());\n"
        "                    const bool unresolvedSignal3 =\n"
        "                        electronics_.SelectedSystem() == AnteyElectronicSystem::Signal3NavigationPeriscope;\n"
        "                    lastCombatCommand_ = PlayerCombatCommandFeedback{\n"
        "                        .command = command.type, .accepted = mastAvailable && !unresolvedSignal3,\n"
        "                        .trackId = playerCombat_.SelectedTrackId(), .message = *operated};\n"
        "                    continue;\n"
        "                }\n"
        "                if (command.type == PlayerCombatCommandType::PreviousWeapon ||")

    # Project the suite without exposing truth or mutable runtime objects to UI.
    replace_once(path,
        "        ApplyPeriscopePresentation(playerCombatPresentation, periscopeState_, playerSnapshot.signedDepthMeters);\n\n        float sonarOwnshipHeadingRadians",
        "        ApplyPeriscopePresentation(playerCombatPresentation, periscopeState_, playerSnapshot.signedDepthMeters);\n"
        "        const auto& electronicState = electronics_.State();\n"
        "        playerCombatPresentation.selectedElectronicSystem = electronicState.selectedSystem;\n"
        "        playerCombatPresentation.electronicSystemDeployed = electronicState.deployed;\n"
        "        playerCombatPresentation.electronicSystemDeployed[AnteyElectronicSystemIndex(\n"
        "            AnteyElectronicSystem::Pzns10AttackPeriscope)] = periscopeState_.raised;\n"
        "        playerCombatPresentation.radianTransmitting = electronicState.radianTransmitting;\n"
        "        playerCombatPresentation.radioTransmitting = electronics_.RadioTransmitting(simulationTimeSeconds);\n"
        "        playerCombatPresentation.rkpCompressorRequested = electronics_.RkpRequested();\n"
        "        playerCombatPresentation.navigationErrorMeters = electronicState.navigationErrorMeters;\n"
        "        playerCombatPresentation.externalTargetReportSource = electronicState.lastReceivedReportSource;\n"
        "        if (electronicState.lastReceivedReportSource)\n"
        "        {\n"
        "            const auto issueTime = *electronicState.lastReceivedReportSource == ExternalTargetReportSource::Tu95Rts\n"
        "                ? electronicState.lastMrsc2ReportIssueTimeSeconds\n"
        "                : electronicState.lastSelenaReportIssueTimeSeconds;\n"
        "            if (issueTime)\n"
        "            {\n"
        "                const float reportAge = static_cast<float>((std::max)(0.0, simulationTimeSeconds - *issueTime));\n"
        "                playerCombatPresentation.externalTargetReportAgeSeconds = reportAge;\n"
        "                if (electronicState.lastReceivedReportUncertaintyMeters)\n"
        "                    playerCombatPresentation.externalTargetReportUncertaintyMeters =\n"
        "                        *electronicState.lastReceivedReportUncertaintyMeters +\n"
        "                        reportAge * electronics_.Config().externalReportUncertaintyGrowthMetersPerSecond;\n"
        "            }\n"
        "        }\n\n"
        "        float sonarOwnshipHeadingRadians")

    replace_once(path,
        "    PeriscopeState periscopeState_{};\n    PeriscopeOpticalConditions periscopeOpticalConditions_{};",
        "    PeriscopeState periscopeState_{};\n    PeriscopeOpticalConditions periscopeOpticalConditions_{};\n"
        "    AnteyElectronicCombatRuntime electronics_{};")


def patch_physical_mast_presentation() -> None:
    path = ROOT / "Game/PhysicalPlayground.h"
    replace_once(path, "#include <string>\n#include <vector>\n", "#include <string>\n#include <string_view>\n#include <utility>\n#include <vector>\n")
    replace_once(path,
        "    [[nodiscard]] float PeriscopeDeploymentProgress() const noexcept\n    {\n        return primaryPeriscopeDeploymentProgress_;\n    }\n\n    // Presentation-only bridge from the P-700 lifecycle.",
        "    [[nodiscard]] float PeriscopeDeploymentProgress() const noexcept\n    {\n        return primaryPeriscopeDeploymentProgress_;\n    }\n\n"
        "    [[nodiscard]] std::expected<void, std::string> SetRetractableSystemPresentation(\n"
        "        const std::string_view systemRole, const bool raised)\n"
        "    {\n"
        "        for (auto& binding : retractableSystemPresentationBindings_)\n"
        "        {\n"
        "            for (auto& request : binding.systemRequests)\n"
        "            {\n"
        "                if (request.first == systemRole)\n"
        "                {\n"
        "                    request.second = raised;\n"
        "                    return {};\n"
        "                }\n"
        "            }\n"
        "        }\n"
        "        return std::unexpected(\"production retractable-system presentation binding is unavailable: \" +\n"
        "                               std::string(systemRole));\n"
        "    }\n\n"
        "    // Presentation-only bridge from the P-700 lifecycle.")

    replace_once(path,
        "    // IG1-B.1 fixed submerged presentation state. These per-node post transforms are built from opaque\n"
        "    // IG1 production bindings once at initialization and never mutate ModelAsset or physics.\n"
        "    std::vector<Render::ModelNodeTransformOverride> submergedSailDeviceOverrides_;\n",
        "    // IG1-B.1 fixed submerged presentation state. These per-node post transforms are built from opaque\n"
        "    // IG1 production bindings once at initialization and never mutate ModelAsset or physics.\n"
        "    std::vector<Render::ModelNodeTransformOverride> submergedSailDeviceOverrides_;\n"
        "    struct RetractableSystemPresentationBinding final\n"
        "    {\n"
        "        std::size_t nodeIndex = 0U;\n"
        "        std::vector<std::pair<std::string, bool>> systemRequests;\n"
        "        Assets::ModelTransform stowedTransform{};\n"
        "        Assets::ModelTransform deployedTransform{};\n"
        "        float deploymentProgress = 0.0F;\n"
        "    };\n"
        "    std::vector<RetractableSystemPresentationBinding> retractableSystemPresentationBindings_;\n")

    path = ROOT / "Game/PhysicalPlayground.cpp"
    # Build presentation bindings from semantic system roles; PZNS stays on the accepted dedicated periscope path.
    replace_once(path,
        "    std::vector<Render::ModelNodeTransformOverride> submergedSailDeviceOverrides;\n    std::optional<std::size_t> primaryPeriscopeNodeIndex{};",
        "    std::vector<Render::ModelNodeTransformOverride> submergedSailDeviceOverrides;\n"
        "    std::vector<RetractableSystemPresentationBinding> retractableSystemPresentationBindings;\n"
        "    std::optional<std::size_t> primaryPeriscopeNodeIndex{};")
    replace_once(path,
        "        submergedSailDeviceOverrides.push_back({\n            .nodeIndex = device.presentationNodeBindingIndex,\n            .nodeLocalPostTransform = device.stowedLocalPostTransform});\n        if (device.functionalRole == \"PERISCOPE_PRIMARY\")",
        "        submergedSailDeviceOverrides.push_back({\n            .nodeIndex = device.presentationNodeBindingIndex,\n            .nodeLocalPostTransform = device.stowedLocalPostTransform});\n"
        "        if (!device.systemRoles.empty() && device.functionalRole != \"PERISCOPE_PRIMARY\")\n"
        "        {\n"
        "            RetractableSystemPresentationBinding presentationBinding{\n"
        "                .nodeIndex = device.presentationNodeBindingIndex,\n"
        "                .stowedTransform = device.stowedLocalPostTransform,\n"
        "                .deployedTransform = device.deployedLocalPostTransform};\n"
        "            for (const std::string& systemRole : device.systemRoles)\n"
        "                presentationBinding.systemRequests.emplace_back(systemRole, false);\n"
        "            retractableSystemPresentationBindings.push_back(std::move(presentationBinding));\n"
        "        }\n"
        "        if (device.functionalRole == \"PERISCOPE_PRIMARY\")")
    replace_once(path,
        "    submergedSailDeviceOverrides_ = std::move(submergedSailDeviceOverrides);\n    primaryPeriscopeNodeIndex_ = primaryPeriscopeNodeIndex;",
        "    submergedSailDeviceOverrides_ = std::move(submergedSailDeviceOverrides);\n"
        "    retractableSystemPresentationBindings_ = std::move(retractableSystemPresentationBindings);\n"
        "    primaryPeriscopeNodeIndex_ = primaryPeriscopeNodeIndex;")

    replace_once(path,
        "    // Presentation animation follows committed gameplay periscope state but never feeds physics/sensors.\n    const float periscopeStep = fixedDeltaSeconds / M5PrimaryPeriscopeDeploymentSeconds;",
        "    // Presentation animation follows committed gameplay mast state but never feeds physics/sensors.\n"
        "    const float periscopeStep = fixedDeltaSeconds / M5PrimaryPeriscopeDeploymentSeconds;\n"
        "    for (auto& binding : retractableSystemPresentationBindings_)\n"
        "    {\n"
        "        const bool requestedRaised = std::ranges::any_of(\n"
        "            binding.systemRequests, [](const auto& request) { return request.second; });\n"
        "        binding.deploymentProgress = std::clamp(\n"
        "            binding.deploymentProgress + (requestedRaised ? periscopeStep : -periscopeStep), 0.0F, 1.0F);\n"
        "    }\n")

    replace_once(path,
        "    if (primaryPeriscopeNodeIndex_.has_value())\n    {",
        "    for (const auto& binding : retractableSystemPresentationBindings_)\n"
        "    {\n"
        "        Assets::ModelTransform transform = binding.stowedTransform;\n"
        "        for (std::size_t element = 0; element < transform.values.size(); ++element)\n"
        "        {\n"
        "            transform.values[element] = binding.stowedTransform.values[element] +\n"
        "                (binding.deployedTransform.values[element] - binding.stowedTransform.values[element]) *\n"
        "                    binding.deploymentProgress;\n"
        "        }\n"
        "        const auto existing = std::ranges::find_if(submarineNodeOverrides, [&](const auto& value) {\n"
        "            return value.nodeIndex == binding.nodeIndex;\n"
        "        });\n"
        "        if (existing == submarineNodeOverrides.end())\n"
        "            return std::unexpected(\"physical playground retractable-system stowed override disappeared\");\n"
        "        existing->nodeLocalPostTransform = transform;\n"
        "    }\n"
        "    if (primaryPeriscopeNodeIndex_.has_value())\n    {")


def patch_ui() -> None:
    path = ROOT / "Game/Combat/CombatCommandUi.h"
    replace_once(path,
        "    float dynamicMassKg = 0.0F;\n    bool bowPlanesDeployed = true;",
        "    float dynamicMassKg = 0.0F;\n    float highPressureAirFraction = 1.0F;\n"
        "    bool rkpCompressorRunning = false;\n    bool bowPlanesDeployed = true;")

    path = ROOT / "Game/Combat/CombatCommandUi.cpp"
    replace_once(path,
        "    case PlayerCombatCommandType::ToggleP700SalvoMode: return \"P-700 SALVO MODE\";\n    }",
        "    case PlayerCombatCommandType::ToggleP700SalvoMode: return \"P-700 SALVO MODE\";\n"
        "    case PlayerCombatCommandType::CycleElectronicSuite: return \"ELECTRONICS SELECT\";\n"
        "    case PlayerCombatCommandType::OperateElectronicSuite: return \"ELECTRONICS OPERATE\";\n"
        "    }")
    replace_once(path,
        "            ImGui::Text(\"Optical detail: %s\", OpticalDetailName(snapshot.selectedTrackOpticalIdentificationLevel));",
        "            ImGui::Text(\"Sources: %s%s%s%s%s%s\",\n"
        "                snapshot.selectedTrackHasPassiveAcousticEvidence ? \"PASSIVE \" : \"\",\n"
        "                snapshot.selectedTrackHasActiveAcousticEvidence ? \"ACTIVE \" : \"\",\n"
        "                snapshot.selectedTrackHasElectronicSupportEvidence ? \"ESM \" : \"\",\n"
        "                snapshot.selectedTrackHasSurfaceRadarEvidence ? \"RADAR \" : \"\",\n"
        "                snapshot.selectedTrackHasExternalReportEvidence ? \"EXT-CU \" : \"\",\n"
        "                snapshot.selectedTrackHasOpticalEvidence ? \"OPTICAL\" : \"\");\n"
        "            if (snapshot.selectedTrackExternalReportAgeSeconds)\n"
        "                ImGui::Text(\"External-CU evidence age: %.0f s\", *snapshot.selectedTrackExternalReportAgeSeconds);\n"
        "            ImGui::Text(\"Optical detail: %s\", OpticalDetailName(snapshot.selectedTrackOpticalIdentificationLevel));")
    replace_once(path,
        "    ImGui::Text(\"Visual ID: %s\", snapshot.canVisualIdentify ? \"READY\" : \"UNAVAILABLE\");\n\n    ImGui::Separator();\n    ImGui::TextUnformatted(\"THREAT\");",
        "    ImGui::Text(\"Visual ID: %s\", snapshot.canVisualIdentify ? \"READY\" : \"UNAVAILABLE\");\n\n"
        "    ImGui::Separator();\n"
        "    ImGui::TextUnformatted(\"ELECTRONICS / MASTS\");\n"
        "    const std::string_view selectedElectronicName = AnteyElectronicSystemName(snapshot.selectedElectronicSystem);\n"
        "    const bool selectedElectronicRaised = snapshot.electronicSystemDeployed[\n"
        "        AnteyElectronicSystemIndex(snapshot.selectedElectronicSystem)];\n"
        "    ImGui::Text(\"Selected: %.*s [%s]\", static_cast<int>(selectedElectronicName.size()),\n"
        "                selectedElectronicName.data(), selectedElectronicRaised ? \"RAISED\" : \"STOWED\");\n"
        "    ImGui::Text(\"RADIAN: %s\", snapshot.radianTransmitting ? \"TRANSMITTING / INTERCEPT RISK\" : \"EMCON\");\n"
        "    ImGui::Text(\"Radio: %s\", snapshot.radioTransmitting ? \"BURST TX / INTERCEPT RISK\" : \"SILENT\");\n"
        "    ImGui::Text(\"INS position error: +/- %.0f m\", snapshot.navigationErrorMeters);\n"
        "    if (snapshot.externalTargetReportSource)\n"
        "    {\n"
        "        const std::string_view reportSource = ExternalTargetReportSourceName(*snapshot.externalTargetReportSource);\n"
        "        ImGui::Text(\"External CU: %.*s | age %.0f s | unc +/- %.1f km\",\n"
        "                    static_cast<int>(reportSource.size()), reportSource.data(),\n"
        "                    snapshot.externalTargetReportAgeSeconds.value_or(0.0F),\n"
        "                    snapshot.externalTargetReportUncertaintyMeters.value_or(0.0F) / 1000.0F);\n"
        "    }\n"
        "    else\n"
        "        ImGui::TextUnformatted(\"External CU: NONE\");\n"
        "    ImGui::Text(\"HP air: %.0f%% | RKP: %s\", snapshot.highPressureAirFraction * 100.0F,\n"
        "                snapshot.rkpCompressorRunning ? \"RUNNING\" :\n"
        "                (snapshot.rkpCompressorRequested ? \"REQUESTED\" : \"OFF\"));\n\n"
        "    ImGui::Separator();\n    ImGui::TextUnformatted(\"THREAT\");")
    replace_once(path,
        "    ImGui::TextUnformatted(\"X / F            Deploy decoy\");\n    ImGui::End();",
        "    ImGui::TextUnformatted(\"X / F            Deploy decoy\");\n"
        "    ImGui::TextUnformatted(\"LB / R           Select electronic/mast system\");\n"
        "    ImGui::TextUnformatted(\"B / =            Raise/operate selected system\");\n"
        "    ImGui::End();")
    replace_once(path,
        "    ImGui::Text(\"Main ballast: %.0f%% / %s | Trim: %+0.1f t\",\n                snapshot.mainBallastFillFraction * 100.0F,\n                MainBallastStateName(snapshot),\n                snapshot.trimMassDeltaKg / 1000.0F);",
        "    ImGui::Text(\"Main ballast: %.0f%% / %s | Trim: %+0.1f t\",\n"
        "                snapshot.mainBallastFillFraction * 100.0F,\n"
        "                MainBallastStateName(snapshot),\n"
        "                snapshot.trimMassDeltaKg / 1000.0F);\n"
        "    ImGui::Text(\"HP air: %.0f%% | RKP compressor: %s\",\n"
        "                snapshot.highPressureAirFraction * 100.0F,\n"
        "                snapshot.rkpCompressorRunning ? \"RUNNING\" : \"OFF\");")


def patch_main() -> None:
    path = ROOT / "DeepRun/Main.cpp"
    replace_once(path,
        "        std::uint64_t consumedP700SalvoModeSequence = 0;\n        bool loggedHapticSubmissionFailure",
        "        std::uint64_t consumedP700SalvoModeSequence = 0;\n"
        "        std::uint64_t consumedCycleElectronicSuiteSequence = 0;\n"
        "        std::uint64_t consumedOperateElectronicSuiteSequence = 0;\n"
        "        bool loggedHapticSubmissionFailure")
    replace_once(path,
        "             &consumedTogglePeriscopeSequence, &consumedVisualIdentifySequence, &consumedP700SalvoModeSequence,\n             &loggedHapticSubmissionFailure",
        "             &consumedTogglePeriscopeSequence, &consumedVisualIdentifySequence, &consumedP700SalvoModeSequence,\n"
        "             &consumedCycleElectronicSuiteSequence, &consumedOperateElectronicSuiteSequence,\n"
        "             &loggedHapticSubmissionFailure")
    replace_once(path,
        "                    std::array<DeepRun::Game::Combat::PlayerCombatCommand, 10> playerCommands{};",
        "                    std::array<DeepRun::Game::Combat::PlayerCombatCommand, 12> playerCommands{};")
    replace_once(path,
        "                        consume(*inputState, DeepRun::Input::InputAction::DeployDecoy,\n                                DeepRun::Game::Combat::PlayerCombatCommandType::DeployDecoy,\n                                consumedDeployDecoySequence);",
        "                        consume(*inputState, DeepRun::Input::InputAction::DeployDecoy,\n"
        "                                DeepRun::Game::Combat::PlayerCombatCommandType::DeployDecoy,\n"
        "                                consumedDeployDecoySequence);\n"
        "                        consume(*inputState, DeepRun::Input::InputAction::CycleElectronicSuite,\n"
        "                                DeepRun::Game::Combat::PlayerCombatCommandType::CycleElectronicSuite,\n"
        "                                consumedCycleElectronicSuiteSequence);\n"
        "                        consume(*inputState, DeepRun::Input::InputAction::OperateElectronicSuite,\n"
        "                                DeepRun::Game::Combat::PlayerCombatCommandType::OperateElectronicSuite,\n"
        "                                consumedOperateElectronicSuiteSequence);")

    replace_once(path,
        "                    combatUiSnapshot = combatFrame->playerCombat;\n                    const auto periscopePresentation =",
        "                    combatUiSnapshot = combatFrame->playerCombat;\n"
        "                    playground.SetRkpCompressorRequested(combatFrame->playerCombat.rkpCompressorRequested);\n"
        "                    const auto physicalTelemetry = playground.BuildVesselPresentationTelemetry();\n"
        "                    if (!physicalTelemetry)\n"
        "                    {\n"
        "                        std::cerr << \"[Game][ERROR] electronics physical telemetry failed: \"\n"
        "                                  << physicalTelemetry.error() << '\\n';\n"
        "                        return false;\n"
        "                    }\n"
        "                    combatUiSnapshot->highPressureAirFraction = physicalTelemetry->highPressureAirFraction;\n"
        "                    combatUiSnapshot->rkpCompressorRunning = physicalTelemetry->rkpCompressorRunning;\n"
        "                    for (std::size_t systemIndex = 0;\n"
        "                         systemIndex < DeepRun::Game::Combat::AnteyElectronicSystemCount; ++systemIndex)\n"
        "                    {\n"
        "                        const auto system = static_cast<DeepRun::Game::Combat::AnteyElectronicSystem>(systemIndex);\n"
        "                        if (system == DeepRun::Game::Combat::AnteyElectronicSystem::Pzns10AttackPeriscope ||\n"
        "                            system == DeepRun::Game::Combat::AnteyElectronicSystem::Signal3NavigationPeriscope)\n"
        "                            continue;\n"
        "                        const auto mastPresentation = playground.SetRetractableSystemPresentation(\n"
        "                            DeepRun::Game::Combat::AnteyElectronicSystemProductionRoleId(system),\n"
        "                            combatFrame->playerCombat.electronicSystemDeployed[systemIndex]);\n"
        "                        if (!mastPresentation)\n"
        "                        {\n"
        "                            std::cerr << \"[Game][ERROR] production electronics mast presentation failed: \"\n"
        "                                      << mastPresentation.error() << '\\n';\n"
        "                            return false;\n"
        "                        }\n"
        "                    }\n"
        "                    const auto periscopePresentation =")

    replace_once(path,
        "                            .dynamicMassKg = navigationTelemetry->dynamicMassKg,\n                            .bowPlanesDeployed = navigationTelemetry->bowPlanesDeployed,",
        "                            .dynamicMassKg = navigationTelemetry->dynamicMassKg,\n"
        "                            .highPressureAirFraction = navigationTelemetry->highPressureAirFraction,\n"
        "                            .rkpCompressorRunning = navigationTelemetry->rkpCompressorRunning,\n"
        "                            .bowPlanesDeployed = navigationTelemetry->bowPlanesDeployed,")


def patch_tests() -> None:
    path = ROOT / "Tests/AnteyElectronicSuiteTest.cpp"
    replace_once(path,
        '#include "Simulation/Perception/TrackManager.h"\n',
        '#include "Simulation/Perception/TrackManager.h"\n#include "Engine/Input/InputSystem.h"\n')
    replace_once(path,
        "    std::cout << \"[AnteyElectronicSuiteTest][PASS] ESM radar external-CU navigation radio RKP and dual-optics contracts\\n\";",
        "    Input::GamepadState controls{};\n"
        "    controls.connected = true;\n"
        "    controls.buttons = static_cast<std::uint16_t>(Input::GamepadButton::LeftShoulder) |\n"
        "                       static_cast<std::uint16_t>(Input::GamepadButton::B);\n"
        "    const auto semanticActions = Input::SemanticActionsForGamepad(controls);\n"
        "    Require(semanticActions.cycleElectronicSuite && semanticActions.operateElectronicSuite,\n"
        "            \"controller must expose electronic-suite select/operate actions\");\n"
        "    Require(AnteyElectronicSystemProductionRoleId(AnteyElectronicSystem::RadianSurfaceRadar) ==\n"
        "                std::string_view{\"RADIAN_SURFACE_RADAR\"},\n"
        "            \"electronic system semantic role contract mismatch\");\n\n"
        "    std::cout << \"[AnteyElectronicSuiteTest][PASS] ESM radar external-CU navigation radio RKP dual-optics and controls\\n\";")


def main() -> None:
    patch_electronic_suite()
    patch_player_combat_contract()
    patch_input()
    patch_combat_runtime()
    patch_physical_mast_presentation()
    patch_ui()
    patch_main()
    patch_tests()
    print("Antey electronics gameplay integration patch: PASS")


if __name__ == "__main__":
    main()
