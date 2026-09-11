$ErrorActionPreference = 'Stop'

function Read-Lf([string]$Path) {
    return (Get-Content -Raw $Path).Replace("`r`n", "`n")
}
function Write-Lf([string]$Path, [string]$Content) {
    [System.IO.File]::WriteAllText($Path, $Content, [System.Text.UTF8Encoding]::new($false))
}
function Replace-Once([string]$Path, [string]$Old, [string]$New) {
    $text = Read-Lf $Path
    $first = $text.IndexOf($Old, [System.StringComparison]::Ordinal)
    if ($first -lt 0) { throw "Anchor not found in $Path" }
    if ($text.IndexOf($Old, $first + $Old.Length, [System.StringComparison]::Ordinal) -ge 0) {
        throw "Anchor is not unique in $Path"
    }
    $text = $text.Substring(0, $first) + $New + $text.Substring($first + $Old.Length)
    Write-Lf $Path $text
}

# Semantic actions: converge the M5 combat bindings on D1 and add ActiveSonarPing.
Replace-Once 'Engine/Input/InputState.h' @'
    FireWeapon,
    DeployDecoy,
    Count,
'@ @'
    FireWeapon,
    ActiveSonarPing,
    DeployDecoy,
    Count,
'@

Replace-Once 'Engine/Input/InputSystem.h' @'
    bool fireWeapon = false;
    bool deployDecoy = false;
'@ @'
    bool fireWeapon = false;
    bool activeSonarPing = false;
    bool deployDecoy = false;
'@
Replace-Once 'Engine/Input/InputSystem.h' @'
    bool prepareWeaponKeyDown_ = false;
    bool fireWeaponKeyDown_ = false;
    bool deployDecoyKeyDown_ = false;
'@ @'
    bool prepareWeaponKeyDown_ = false;
    bool activeSonarPingKeyDown_ = false;
    bool deployDecoyKeyDown_ = false;
'@

Replace-Once 'Engine/Input/InputSystem.cpp' @'
    const float leftTrigger = std::isfinite(gamepad.leftTrigger)
                                  ? std::clamp(gamepad.leftTrigger, 0.0F, 1.0F)
                                  : 0.0F;
    const float rightTrigger = std::isfinite(gamepad.rightTrigger)
                                   ? std::clamp(gamepad.rightTrigger, 0.0F, 1.0F)
                                   : 0.0F;
    result.cameraZoom = rightTrigger - leftTrigger;
'@ @'
    // M5-J5 gives LT/RT back to the canonical weapon semantics. In the current tactical-camera context,
    // right-stick X pans and right-stick Y supplies controller zoom so camera control remains controller-complete.
    result.cameraPanY = 0.0F;
    result.cameraZoom = -camera.y;
'@
Replace-Once 'Engine/Input/InputSystem.cpp' @'
    return ControllerSemanticActions{
        .selectContact = HasGamepadButton(gamepad, GamepadButton::Y),
        .prepareWeapon = HasGamepadButton(gamepad, GamepadButton::X),
        .fireWeapon = HasGamepadButton(gamepad, GamepadButton::A),
        .deployDecoy = HasGamepadButton(gamepad, GamepadButton::B)};
'@ @'
    constexpr float TriggerActionThreshold = 0.50F;
    const float leftTrigger = std::isfinite(gamepad.leftTrigger)
                                  ? std::clamp(gamepad.leftTrigger, 0.0F, 1.0F)
                                  : 0.0F;
    const float rightTrigger = std::isfinite(gamepad.rightTrigger)
                                   ? std::clamp(gamepad.rightTrigger, 0.0F, 1.0F)
                                   : 0.0F;
    return ControllerSemanticActions{
        .selectContact = HasGamepadButton(gamepad, GamepadButton::Y),
        .prepareWeapon = leftTrigger >= TriggerActionThreshold,
        .fireWeapon = rightTrigger >= TriggerActionThreshold,
        .activeSonarPing = HasGamepadButton(gamepad, GamepadButton::RightShoulder),
        .deployDecoy = HasGamepadButton(gamepad, GamepadButton::X)};
'@
Replace-Once 'Engine/Input/InputSystem.cpp' @'
            else if (event.key == Platform::Key::Space)
            {
                fireWeaponKeyDown_ = true;
            }
'@ @'
            else if (event.key == Platform::Key::Space)
            {
                activeSonarPingKeyDown_ = true;
            }
'@
Replace-Once 'Engine/Input/InputSystem.cpp' @'
            else if (event.key == Platform::Key::Space)
            {
                fireWeaponKeyDown_ = false;
            }
'@ @'
            else if (event.key == Platform::Key::Space)
            {
                activeSonarPingKeyDown_ = false;
            }
'@
Replace-Once 'Engine/Input/InputSystem.cpp' @'
    state_.SetActionDown(
        InputAction::FireWeapon,
        fireWeaponKeyDown_ ||
            state_.IsMouseButtonDown(static_cast<std::size_t>(Platform::MouseButton::Left)) ||
            controller.fireWeapon);
    state_.SetActionDown(InputAction::DeployDecoy, deployDecoyKeyDown_ || controller.deployDecoy);
'@ @'
    state_.SetActionDown(
        InputAction::FireWeapon,
        state_.IsMouseButtonDown(static_cast<std::size_t>(Platform::MouseButton::Left)) ||
            controller.fireWeapon);
    state_.SetActionDown(
        InputAction::ActiveSonarPing,
        activeSonarPingKeyDown_ || controller.activeSonarPing);
    state_.SetActionDown(InputAction::DeployDecoy, deployDecoyKeyDown_ || controller.deployDecoy);
'@

# Commander command/presentation contract.
Replace-Once 'Game/Combat/PlayerCombatCommandRuntime.h' @'
    FireWeapon,
    DeployDecoy,
};
'@ @'
    FireWeapon,
    ActiveSonarPing,
    DeployDecoy,
};
'@
Replace-Once 'Game/Combat/PlayerCombatCommandRuntime.h' @'
    bool canFireWeapon = false;

    // M5-J3 is populated by CombatPlaygroundRuntime from a dedicated passive-acoustic perceived-world path.
'@ @'
    bool canFireWeapon = false;
    bool canActiveSonarPing = false;
    bool activeSonarPulsePending = false;

    // M5-J3 is populated by CombatPlaygroundRuntime from a dedicated passive-acoustic perceived-world path.
'@
Replace-Once 'Game/Combat/PlayerCombatCommandRuntime.h' @'
        case PlayerCombatCommandType::FireWeapon:
            return Fire(tracks, simulationTimeSeconds);
        case PlayerCombatCommandType::DeployDecoy:
            return std::unexpected("M5-J4 DeployDecoy is owned by CombatPlaygroundRuntime, not weapon runtime");
'@ @'
        case PlayerCombatCommandType::FireWeapon:
            return Fire(tracks, simulationTimeSeconds);
        case PlayerCombatCommandType::ActiveSonarPing:
            return std::unexpected("M5-J5 ActiveSonarPing is owned by CombatPlaygroundRuntime, not weapon runtime");
        case PlayerCombatCommandType::DeployDecoy:
            return std::unexpected("M5-J4 DeployDecoy is owned by CombatPlaygroundRuntime, not weapon runtime");
'@

# Live combat runtime: add a passive player contact, remove normal-play auto-ranging, and execute ping by selected Track bearing.
Replace-Once 'Game/Combat/CombatPlaygroundRuntime.h' @'
    [[nodiscard]] bool PlayerDecoyAvailable() const noexcept { return playerDecoyAvailable_; }
    [[nodiscard]] const Weapons::TorpedoSeekerRuntimeState& DestroyerTorpedoSeekerState() const noexcept
'@ @'
    [[nodiscard]] bool PlayerDecoyAvailable() const noexcept { return playerDecoyAvailable_; }
    [[nodiscard]] const std::optional<Acoustics::ActiveAcousticPulse>& PlayerActivePulse() const noexcept
    {
        return activePulse_;
    }
    [[nodiscard]] const Weapons::TorpedoSeekerRuntimeState& DestroyerTorpedoSeekerState() const noexcept
'@
Replace-Once 'Game/Combat/CombatPlaygroundRuntime.h' @'
        if (!activePulse_.has_value() && simulationTimeSeconds >= nextActivePulseTimeSeconds_)
        {
            const Physics::PhysicsVector3 delta = Difference(
                destroyerAcoustics->emitter.positionMeters, playerSnapshot.passiveReceiver.positionMeters);
            const auto direction = Normalize(delta);
            if (!direction)
            {
                return std::unexpected("M5-H active pulse direction is invalid");
            }
            activePulse_ = Acoustics::ActiveAcousticPulse{
                .originMeters = playerSnapshot.passiveReceiver.positionMeters,
                .forwardUnitVector = *direction,
                .sourceLevelDb = {.levelDb = {230.0F, 232.0F, 234.0F, 230.0F}},
                .beamHalfAngleRadians = 0.35F,
                .emissionTimeSeconds = simulationTimeSeconds};
            activeReflector_ = Acoustics::AcousticReflector{
                .positionMeters = destroyerAcoustics->emitter.positionMeters,
                .reflectionLossDb = {.levelDb = {8.0F, 8.0F, 8.0F, 8.0F}}};
        }

        bool integratedActiveEcho = false;
'@ @'
        // M5-J5 gives normal play a legitimate bearing-only passive contact before any active ranging. Continuous
        // source sampling uses the same bounded direct acoustic propagation already used by the reciprocal side;
        // FromAcousticObservation deliberately receives no own-position argument, so no range/position is fabricated.
        bool integratedPlayerEvidence = false;
        const double playerPassiveDistance = Distance(
            destroyerAcoustics->emitter.positionMeters, playerSnapshot.passiveReceiver.positionMeters);
        const double playerPassiveTravelSeconds = playerPassiveDistance /
            static_cast<double>(acousticWorld_.Config().effectiveSoundSpeedMetersPerSecond);
        const double destroyerEmissionTime = simulationTimeSeconds > playerPassiveTravelSeconds
            ? std::max(0.0, simulationTimeSeconds - playerPassiveTravelSeconds - 1.0e-6)
            : 0.0;
        const Acoustics::AcousticEmission destroyerEmission{
            .positionMeters = destroyerAcoustics->emitter.positionMeters,
            .sourceLevelDb = destroyerAcoustics->emitter.continuousSourceLevelDb,
            .emissionTimeSeconds = destroyerEmissionTime};
        const auto playerObserved = acousticWorld_.CollectPassiveDirectObservation(
            destroyerEmission, playerSnapshot.passiveReceiver, simulationTimeSeconds);
        if (!playerObserved)
        {
            return std::unexpected("M5-J5 player passive propagation failed: " + playerObserved.error().message);
        }
        if (playerObserved->has_value())
        {
            const auto perceived = Perception::FromAcousticObservation(**playerObserved);
            if (!perceived || !playerTracks_.IntegrateObservation(*perceived))
            {
                return std::unexpected("M5-J5 player passive evidence failed perception integration");
            }
            integratedPlayerEvidence = true;
        }

        // The accepted automated smoke path retains its deterministic ranging helper. Normal play never enters
        // this branch: it must issue ActiveSonarPing against a selected perceived Track below.
        if (automatedPlayer && !activePulse_.has_value() && simulationTimeSeconds >= nextActivePulseTimeSeconds_)
        {
            const Physics::PhysicsVector3 delta = Difference(
                destroyerAcoustics->emitter.positionMeters, playerSnapshot.passiveReceiver.positionMeters);
            const auto direction = Normalize(delta);
            if (!direction)
            {
                return std::unexpected("M5-H active pulse direction is invalid");
            }
            activePulse_ = Acoustics::ActiveAcousticPulse{
                .originMeters = playerSnapshot.passiveReceiver.positionMeters,
                .forwardUnitVector = *direction,
                .sourceLevelDb = {.levelDb = {230.0F, 232.0F, 234.0F, 230.0F}},
                .beamHalfAngleRadians = 0.35F,
                .emissionTimeSeconds = simulationTimeSeconds};
            activeReflector_ = Acoustics::AcousticReflector{
                .positionMeters = destroyerAcoustics->emitter.positionMeters,
                .reflectionLossDb = {.levelDb = {8.0F, 8.0F, 8.0F, 8.0F}}};
        }

        bool integratedActiveEcho = false;
'@
Replace-Once 'Game/Combat/CombatPlaygroundRuntime.h' @'
                integratedActiveEcho = true;
                activePulse_.reset();
'@ @'
                integratedActiveEcho = true;
                integratedPlayerEvidence = true;
                activePulse_.reset();
'@
Replace-Once 'Game/Combat/CombatPlaygroundRuntime.h' @'
        if (!integratedActiveEcho && !playerTracks_.AdvanceTo(simulationTimeSeconds))
'@ @'
        if (!integratedPlayerEvidence && !integratedActiveEcho && !playerTracks_.AdvanceTo(simulationTimeSeconds))
'@
Replace-Once 'Game/Combat/CombatPlaygroundRuntime.h' @'
            for (const PlayerCombatCommand command : commands)
            {
                if (command.type == PlayerCombatCommandType::DeployDecoy)
'@ @'
            for (const PlayerCombatCommand command : commands)
            {
                if (command.type == PlayerCombatCommandType::ActiveSonarPing)
                {
                    const auto feedback = ExecutePlayerActiveSonarCommand(
                        playerSnapshot, *destroyerAcoustics, simulationTimeSeconds);
                    if (!feedback)
                    {
                        return std::unexpected("M5-J5 player active-sonar command failed: " + feedback.error());
                    }
                    lastCombatCommand_ = *feedback;
                    continue;
                }
                if (command.type == PlayerCombatCommandType::DeployDecoy)
'@
Replace-Once 'Game/Combat/CombatPlaygroundRuntime.h' @'
        ApplyIncomingThreatPresentation(playerCombatPresentation);
        playerCombatPresentation.canDeployDecoy = playerDecoyAvailable_;
'@ @'
        ApplyIncomingThreatPresentation(playerCombatPresentation);
        const auto selectedPlayerTrack = FindTrack(
            playerTrackSnapshot, playerCombat_.SelectedTrackId());
        playerCombatPresentation.canActiveSonarPing = selectedPlayerTrack.has_value() &&
            selectedPlayerTrack->lifecycle != Perception::TrackLifecycleState::Lost &&
            !activePulse_.has_value() && simulationTimeSeconds + 1.0e-9 >= nextActivePulseTimeSeconds_;
        playerCombatPresentation.activeSonarPulsePending = activePulse_.has_value();
        playerCombatPresentation.canDeployDecoy = playerDecoyAvailable_;
'@
Replace-Once 'Game/Combat/CombatPlaygroundRuntime.h' @'
    [[nodiscard]] std::expected<PlayerCombatCommandFeedback, std::string> ExecutePlayerDecoyCommand(
'@ @'
    [[nodiscard]] std::expected<PlayerCombatCommandFeedback, std::string> ExecutePlayerActiveSonarCommand(
        const Submarine::AnteyAcousticSnapshot& playerSnapshot,
        const SimpleDestroyerAcousticSnapshot& destroyerAcoustics,
        const double simulationTimeSeconds)
    {
        if (!std::isfinite(simulationTimeSeconds) || !playerSnapshot.passiveReceiver.positionMeters.IsFinite() ||
            !destroyerAcoustics.emitter.positionMeters.IsFinite())
        {
            return std::unexpected("M5-J5 active-sonar command input is invalid");
        }
        const auto selectedTrack = FindTrack(playerTracks_.Tracks(), playerCombat_.SelectedTrackId());
        if (!selectedTrack || selectedTrack->lifecycle == Perception::TrackLifecycleState::Lost ||
            !std::isfinite(selectedTrack->estimatedBearingRadians))
        {
            return PlayerCombatCommandFeedback{
                .command = PlayerCombatCommandType::ActiveSonarPing,
                .accepted = false,
                .trackId = playerCombat_.SelectedTrackId(),
                .message = "active sonar requires a selected perceived contact"};
        }
        if (activePulse_)
        {
            return PlayerCombatCommandFeedback{
                .command = PlayerCombatCommandType::ActiveSonarPing,
                .accepted = false,
                .trackId = selectedTrack->trackId,
                .message = "active sonar pulse is already awaiting its echo"};
        }
        if (simulationTimeSeconds + 1.0e-9 < nextActivePulseTimeSeconds_)
        {
            return PlayerCombatCommandFeedback{
                .command = PlayerCombatCommandType::ActiveSonarPing,
                .accepted = false,
                .trackId = selectedTrack->trackId,
                .message = "active sonar is cooling down"};
        }

        const float bearing = selectedTrack->estimatedBearingRadians;
        activePulse_ = Acoustics::ActiveAcousticPulse{
            .originMeters = playerSnapshot.passiveReceiver.positionMeters,
            .forwardUnitVector = {
                .x = static_cast<float>(std::cos(static_cast<double>(bearing))),
                .y = static_cast<float>(std::sin(static_cast<double>(bearing))),
                .z = 0.0F},
            .sourceLevelDb = {.levelDb = {230.0F, 232.0F, 234.0F, 230.0F}},
            .beamHalfAngleRadians = 0.35F,
            .emissionTimeSeconds = simulationTimeSeconds};
        // Authoritative target position is used only by the acoustic simulator as reflector state. It never
        // enters command feedback, PlayerCombatPresentationSnapshot, Track identity, or weapon target state.
        activeReflector_ = Acoustics::AcousticReflector{
            .positionMeters = destroyerAcoustics.emitter.positionMeters,
            .reflectionLossDb = {.levelDb = {8.0F, 8.0F, 8.0F, 8.0F}}};
        return PlayerCombatCommandFeedback{
            .command = PlayerCombatCommandType::ActiveSonarPing,
            .accepted = true,
            .trackId = selectedTrack->trackId,
            .message = "active sonar ping emitted on selected contact bearing"};
    }

    [[nodiscard]] std::expected<PlayerCombatCommandFeedback, std::string> ExecutePlayerDecoyCommand(
'@

# Player-facing UI and live input plumbing.
Replace-Once 'Game/Combat/CombatCommandUi.cpp' @'
    case PlayerCombatCommandType::FireWeapon: return "FIRE WEAPON";
    case PlayerCombatCommandType::DeployDecoy: return "DEPLOY DECOY";
'@ @'
    case PlayerCombatCommandType::FireWeapon: return "FIRE WEAPON";
    case PlayerCombatCommandType::ActiveSonarPing: return "ACTIVE SONAR";
    case PlayerCombatCommandType::DeployDecoy: return "DEPLOY DECOY";
'@
Replace-Once 'Game/Combat/CombatCommandUi.cpp' @'
    ImGui::Text("Prepare available: %s", snapshot.canPrepareWeapon ? "YES" : "NO");
    ImGui::Text("Fire available: %s", snapshot.canFireWeapon ? "YES" : "NO");
'@ @'
    ImGui::Text("Prepare available: %s", snapshot.canPrepareWeapon ? "YES" : "NO");
    ImGui::Text("Fire available: %s", snapshot.canFireWeapon ? "YES" : "NO");
    ImGui::Text("Active sonar: %s",
                snapshot.activeSonarPulsePending ? "PING OUT" : (snapshot.canActiveSonarPing ? "READY" : "UNAVAILABLE"));
'@
Replace-Once 'Game/Combat/CombatCommandUi.cpp' @'
    ImGui::TextUnformatted("Y / Tab          Select contact");
    ImGui::TextUnformatted("X / R / RMB      Prepare weapon");
    ImGui::TextUnformatted("A / Space / LMB  Fire weapon");
    ImGui::TextUnformatted("B / F            Deploy decoy");
'@ @'
    ImGui::TextUnformatted("Y / Tab          Select contact");
    ImGui::TextUnformatted("LT / R / RMB     Prepare weapon");
    ImGui::TextUnformatted("RT / LMB         Fire weapon");
    ImGui::TextUnformatted("RB / Space       Active sonar ping");
    ImGui::TextUnformatted("X / F            Deploy decoy");
'@

Replace-Once 'DeepRun/Main.cpp' @'
        std::uint64_t consumedFireWeaponSequence = 0;
        std::uint64_t consumedDeployDecoySequence = 0;
'@ @'
        std::uint64_t consumedFireWeaponSequence = 0;
        std::uint64_t consumedActiveSonarPingSequence = 0;
        std::uint64_t consumedDeployDecoySequence = 0;
'@
Replace-Once 'DeepRun/Main.cpp' @'
             &consumedSelectContactSequence, &consumedPrepareWeaponSequence, &consumedFireWeaponSequence,
             &consumedDeployDecoySequence,
'@ @'
             &consumedSelectContactSequence, &consumedPrepareWeaponSequence, &consumedFireWeaponSequence,
             &consumedActiveSonarPingSequence, &consumedDeployDecoySequence,
'@
Replace-Once 'DeepRun/Main.cpp' @'
                    std::array<DeepRun::Game::Combat::PlayerCombatCommand, 4> playerCommands{};
'@ @'
                    std::array<DeepRun::Game::Combat::PlayerCombatCommand, 5> playerCommands{};
'@
Replace-Once 'DeepRun/Main.cpp' @'
                        consume(*inputState, DeepRun::Input::InputAction::FireWeapon,
                                DeepRun::Game::Combat::PlayerCombatCommandType::FireWeapon,
                                consumedFireWeaponSequence);
                        consume(*inputState, DeepRun::Input::InputAction::DeployDecoy,
'@ @'
                        consume(*inputState, DeepRun::Input::InputAction::FireWeapon,
                                DeepRun::Game::Combat::PlayerCombatCommandType::FireWeapon,
                                consumedFireWeaponSequence);
                        consume(*inputState, DeepRun::Input::InputAction::ActiveSonarPing,
                                DeepRun::Game::Combat::PlayerCombatCommandType::ActiveSonarPing,
                                consumedActiveSonarPingSequence);
                        consume(*inputState, DeepRun::Input::InputAction::DeployDecoy,
'@

# Input regression now enforces D1 combat bindings and that weapon triggers no longer zoom the camera.
Write-Lf 'Tests/M5PlayerCombatInputChecks.h' @'
#pragma once

#include "Engine/Diagnostics/Logger.h"
#include "Engine/Input/InputSystem.h"

#include <array>
#include <cmath>
#include <cstdint>

namespace DeepRun::Tests
{
[[nodiscard]] inline bool RunM5PlayerCombatInputChecks()
{
    using namespace Input;

    const std::uint16_t combatButtons =
        static_cast<std::uint16_t>(GamepadButton::Y) |
        static_cast<std::uint16_t>(GamepadButton::X) |
        static_cast<std::uint16_t>(GamepadButton::RightShoulder);
    const auto disconnected = SemanticActionsForGamepad(GamepadState{
        .connected = false,
        .leftTrigger = 1.0F,
        .rightTrigger = 1.0F,
        .buttons = combatButtons});
    if (disconnected.selectContact || disconnected.prepareWeapon || disconnected.fireWeapon ||
        disconnected.activeSonarPing || disconnected.deployDecoy)
    {
        return false;
    }

    const auto controller = SemanticActionsForGamepad(GamepadState{
        .connected = true,
        .leftTrigger = 0.80F,
        .rightTrigger = 0.90F,
        .buttons = combatButtons});
    if (!controller.selectContact || !controller.prepareWeapon || !controller.fireWeapon ||
        !controller.activeSonarPing || !controller.deployDecoy)
    {
        return false;
    }

    // A/B are no longer J2 weapon shortcuts: D1 reserves them for interact/cancel contexts.
    const auto legacyFaceButtons = SemanticActionsForGamepad(GamepadState{
        .connected = true,
        .buttons = static_cast<std::uint16_t>(GamepadButton::A) |
                   static_cast<std::uint16_t>(GamepadButton::B)});
    if (legacyFaceButtons.prepareWeapon || legacyFaceButtons.fireWeapon ||
        legacyFaceButtons.activeSonarPing || legacyFaceButtons.deployDecoy)
    {
        return false;
    }

    const GamepadState thresholdProbe{
        .connected = true,
        .rightY = -0.75F,
        .leftTrigger = 0.20F,
        .rightTrigger = 0.80F,
        .buttons = 0U};
    const auto triggerActions = SemanticActionsForGamepad(thresholdProbe);
    const auto triggerAxes = SemanticAxesForGamepad(thresholdProbe);
    if (triggerActions.selectContact || triggerActions.prepareWeapon || !triggerActions.fireWeapon ||
        triggerActions.activeSonarPing || triggerActions.deployDecoy ||
        triggerAxes.cameraPanY != 0.0F || triggerAxes.cameraZoom <= 0.0F)
    {
        return false;
    }

    Diagnostics::Logger logger;
    InputSystem input(logger, false);

    input.BeginFrame();
    const std::array selectDown{
        Platform::WindowEvent{.type = Platform::WindowEventType::KeyDown, .key = Platform::Key::Tab}};
    input.ProcessEvents(selectDown);
    const std::uint64_t selectSequence = input.State().PressSequence(InputAction::SelectContact);
    if (!input.State().WasPressed(InputAction::SelectContact) ||
        !input.State().IsDown(InputAction::SelectContact) || selectSequence == 0U)
    {
        return false;
    }

    input.BeginFrame();
    input.ProcessEvents({});
    if (input.State().WasPressed(InputAction::SelectContact) ||
        !input.State().IsDown(InputAction::SelectContact) ||
        input.State().PressSequence(InputAction::SelectContact) != selectSequence)
    {
        return false;
    }

    const std::array selectUp{
        Platform::WindowEvent{.type = Platform::WindowEventType::KeyUp, .key = Platform::Key::Tab}};
    input.ProcessEvents(selectUp);
    if (!input.State().WasReleased(InputAction::SelectContact) ||
        input.State().IsDown(InputAction::SelectContact) ||
        input.State().PressSequence(InputAction::SelectContact) != selectSequence)
    {
        return false;
    }

    input.BeginFrame();
    const std::array repeatedSelect{
        Platform::WindowEvent{
            .type = Platform::WindowEventType::KeyDown,
            .key = Platform::Key::Tab,
            .repeated = true}};
    input.ProcessEvents(repeatedSelect);
    if (input.State().WasPressed(InputAction::SelectContact) ||
        input.State().IsDown(InputAction::SelectContact) ||
        input.State().PressSequence(InputAction::SelectContact) != selectSequence)
    {
        return false;
    }

    input.BeginFrame();
    const std::array prepareKeyDown{
        Platform::WindowEvent{.type = Platform::WindowEventType::KeyDown, .key = Platform::Key::R}};
    input.ProcessEvents(prepareKeyDown);
    if (!input.State().WasPressed(InputAction::PrepareWeapon) ||
        !input.State().IsDown(InputAction::PrepareWeapon) || input.State().IsDown(InputAction::FireWeapon) ||
        input.State().PressSequence(InputAction::PrepareWeapon) == 0U)
    {
        return false;
    }
    const std::array prepareKeyUp{
        Platform::WindowEvent{.type = Platform::WindowEventType::KeyUp, .key = Platform::Key::R}};
    input.ProcessEvents(prepareKeyUp);

    input.BeginFrame();
    const std::array pingKeyDown{
        Platform::WindowEvent{.type = Platform::WindowEventType::KeyDown, .key = Platform::Key::Space}};
    input.ProcessEvents(pingKeyDown);
    const std::uint64_t pingSequence = input.State().PressSequence(InputAction::ActiveSonarPing);
    if (!input.State().WasPressed(InputAction::ActiveSonarPing) ||
        !input.State().IsDown(InputAction::ActiveSonarPing) || input.State().IsDown(InputAction::FireWeapon) ||
        pingSequence == 0U)
    {
        return false;
    }
    const std::array pingKeyUp{
        Platform::WindowEvent{.type = Platform::WindowEventType::KeyUp, .key = Platform::Key::Space}};
    input.ProcessEvents(pingKeyUp);
    if (!input.State().WasReleased(InputAction::ActiveSonarPing) ||
        input.State().IsDown(InputAction::ActiveSonarPing) ||
        input.State().PressSequence(InputAction::ActiveSonarPing) != pingSequence)
    {
        return false;
    }

    input.BeginFrame();
    const std::array decoyKeyDown{
        Platform::WindowEvent{.type = Platform::WindowEventType::KeyDown, .key = Platform::Key::F}};
    input.ProcessEvents(decoyKeyDown);
    if (!input.State().WasPressed(InputAction::DeployDecoy) ||
        !input.State().IsDown(InputAction::DeployDecoy) || input.State().IsDown(InputAction::FireWeapon) ||
        input.State().IsDown(InputAction::PrepareWeapon) ||
        input.State().PressSequence(InputAction::DeployDecoy) == 0U)
    {
        return false;
    }
    const std::array decoyKeyUp{
        Platform::WindowEvent{.type = Platform::WindowEventType::KeyUp, .key = Platform::Key::F}};
    input.ProcessEvents(decoyKeyUp);
    if (!input.State().WasReleased(InputAction::DeployDecoy) || input.State().IsDown(InputAction::DeployDecoy))
    {
        return false;
    }

    input.BeginFrame();
    const std::array prepareMouseDown{
        Platform::WindowEvent{
            .type = Platform::WindowEventType::MouseButtonDown,
            .mouseButton = Platform::MouseButton::Right}};
    input.ProcessEvents(prepareMouseDown);
    if (!input.State().WasPressed(InputAction::PrepareWeapon) ||
        !input.State().IsDown(InputAction::PrepareWeapon))
    {
        return false;
    }

    input.BeginFrame();
    const std::array fireMouseDown{
        Platform::WindowEvent{
            .type = Platform::WindowEventType::MouseButtonDown,
            .mouseButton = Platform::MouseButton::Left}};
    input.ProcessEvents(fireMouseDown);
    if (!input.State().WasPressed(InputAction::FireWeapon) ||
        !input.State().IsDown(InputAction::FireWeapon) ||
        !input.State().IsDown(InputAction::PrepareWeapon))
    {
        return false;
    }

    input.BeginFrame();
    const std::array releaseMouse{
        Platform::WindowEvent{
            .type = Platform::WindowEventType::MouseButtonUp,
            .mouseButton = Platform::MouseButton::Right},
        Platform::WindowEvent{
            .type = Platform::WindowEventType::MouseButtonUp,
            .mouseButton = Platform::MouseButton::Left}};
    input.ProcessEvents(releaseMouse);
    if (input.State().IsDown(InputAction::PrepareWeapon) ||
        input.State().IsDown(InputAction::FireWeapon) ||
        !input.State().WasReleased(InputAction::PrepareWeapon) ||
        !input.State().WasReleased(InputAction::FireWeapon))
    {
        return false;
    }

    return true;
}
} // namespace DeepRun::Tests
'@

# Camera regression: weapon triggers are no longer camera zoom; current controller camera uses right-stick Y.
Replace-Once 'Tests/M5MultiScaleCameraChecks.h' @'
    // Controller mapping remains semantic before Game consumes it: right stick X pans, right trigger zooms out,
    // left trigger zooms in. The reserved Y axis may exist in Input, but H.3 camera policy above ignores it.
    const Input::ControllerSemanticAxes controller = Input::SemanticAxesForGamepad(Input::GamepadState{
        .connected = true,
        .rightX = 0.8F,
        .rightY = -0.7F,
        .leftTrigger = 0.1F,
        .rightTrigger = 0.9F});
    if (controller.cameraPanX <= 0.0F || controller.cameraPanY >= 0.0F ||
        std::abs(controller.cameraZoom - 0.8F) > 0.001F)
'@ @'
    // J5 returns LT/RT to Prepare/Fire. In the current tactical-camera context right stick X pans while
    // right stick Y drives continuous zoom; trigger pressure must not leak into camera presentation.
    const Input::ControllerSemanticAxes controller = Input::SemanticAxesForGamepad(Input::GamepadState{
        .connected = true,
        .rightX = 0.8F,
        .rightY = -0.7F,
        .leftTrigger = 0.1F,
        .rightTrigger = 0.9F});
    if (controller.cameraPanX <= 0.0F || controller.cameraPanY != 0.0F ||
        std::abs(controller.cameraZoom - 0.7F) > 0.001F)
'@

# Normal-play regression proves passive bearing -> explicit ping -> delayed spatial Track -> weapon launch.
Write-Lf 'Tests/M5PlayerControlledCombatChecks.h' @'
#pragma once

#include "Game/Combat/CombatPlaygroundRuntime.h"
#include "Game/Submarine/AnteyAcousticModel.h"

#include <array>
#include <cmath>

namespace DeepRun::Tests
{
[[nodiscard]] inline bool RunM5PlayerControlledCombatChecks(Physics::PhysicsWorld& physicsWorld)
{
    using namespace Game::Combat;

    if (!physicsWorld.IsInitialized())
    {
        return false;
    }

    auto runtimeResult = CombatPlaygroundRuntime::Create(physicsWorld, 0.0F, 0.0);
    if (!runtimeResult)
    {
        return false;
    }
    auto runtime = std::move(*runtimeResult);

    const auto playerSnapshotResult = Game::Submarine::BuildAnteyAcousticSnapshot(
        Game::Submarine::AnteyAcousticRuntimeState{
            .bodyReferencePositionMeters = {.x = 0.0F, .y = -100.0F, .z = 0.0F},
            .linearVelocityMetersPerSecond = {},
            .shaftRpm = 35.0F,
            .signedDepthMeters = 100.0F},
        Acoustics::AcousticSpectrum{.levelDb = {43.0F, 41.0F, 39.0F, 37.0F}});
    if (!playerSnapshotResult)
    {
        return false;
    }
    const auto playerSnapshot = *playerSnapshotResult;

    constexpr float fixedDeltaSeconds = 1.0F / 60.0F;
    double commandTimeSeconds = 0.0;
    bool sawPassiveBearingOnlyTrack = false;

    // Normal play must not auto-range. Wait only for the continuous passive destroyer contact and require that
    // it remains bearing-only and therefore insufficient for the position-requiring heavyweight weapon.
    for (int tick = 0; tick <= 180; ++tick)
    {
        commandTimeSeconds = static_cast<double>(tick) * fixedDeltaSeconds;
        const auto frame = runtime.AdvancePlayerControlled(playerSnapshot, {}, commandTimeSeconds);
        if (!frame || frame->playerCombat.selectedTrackId || runtime.PlayerTorpedo() ||
            frame->playerCombat.canActiveSonarPing || frame->playerCombat.activeSonarPulsePending)
        {
            return false;
        }
        for (const auto& track : frame->playerTracks)
        {
            if (track.lifecycle == Perception::TrackLifecycleState::Confirmed)
            {
                if (track.estimatedPositionMeters || track.positionUncertaintyMeters ||
                    Weapons::ValidateTrackForWeapon(runtime.PlayerCombat().WeaponDefinition(), track))
                {
                    return false;
                }
                sawPassiveBearingOnlyTrack = true;
            }
        }
        physicsWorld.Step(fixedDeltaSeconds);
        if (sawPassiveBearingOnlyTrack)
        {
            break;
        }
    }
    if (!sawPassiveBearingOnlyTrack || runtime.PlayerCombat().Weapon().phase != Weapons::WeaponPhase::Stored)
    {
        return false;
    }

    const std::array ping{
        PlayerCombatCommand{.type = PlayerCombatCommandType::ActiveSonarPing}};
    commandTimeSeconds += fixedDeltaSeconds;
    const auto unselectedPing = runtime.AdvancePlayerControlled(playerSnapshot, ping, commandTimeSeconds);
    if (!unselectedPing || !unselectedPing->playerCombat.lastCommand ||
        unselectedPing->playerCombat.lastCommand->accepted ||
        unselectedPing->playerCombat.lastCommand->command != PlayerCombatCommandType::ActiveSonarPing ||
        unselectedPing->playerCombat.activeSonarPulsePending || runtime.PlayerActivePulse())
    {
        return false;
    }
    physicsWorld.Step(fixedDeltaSeconds);

    const std::array select{
        PlayerCombatCommand{.type = PlayerCombatCommandType::SelectNextTrack}};
    commandTimeSeconds += fixedDeltaSeconds;
    const auto selected = runtime.AdvancePlayerControlled(playerSnapshot, select, commandTimeSeconds);
    if (!selected || !selected->playerCombat.selectedTrackId || !selected->playerCombat.canActiveSonarPing ||
        selected->playerCombat.selectedTrackHasEstimatedPosition || !selected->playerCombat.lastCommand ||
        !selected->playerCombat.lastCommand->accepted)
    {
        return false;
    }
    const auto selectedTrackId = *selected->playerCombat.selectedTrackId;
    const auto selectedTrack = std::ranges::find_if(selected->playerTracks, [selectedTrackId](const auto& track) {
        return track.trackId == selectedTrackId;
    });
    if (selectedTrack == selected->playerTracks.end())
    {
        return false;
    }
    const float selectedBearing = selectedTrack->estimatedBearingRadians;
    physicsWorld.Step(fixedDeltaSeconds);

    commandTimeSeconds += fixedDeltaSeconds;
    const double pingEmissionTimeSeconds = commandTimeSeconds;
    const auto emitted = runtime.AdvancePlayerControlled(playerSnapshot, ping, commandTimeSeconds);
    if (!emitted || !emitted->playerCombat.lastCommand || !emitted->playerCombat.lastCommand->accepted ||
        emitted->playerCombat.lastCommand->command != PlayerCombatCommandType::ActiveSonarPing ||
        emitted->playerCombat.lastCommand->trackId != std::optional<std::uint64_t>{selectedTrackId} ||
        !emitted->playerCombat.activeSonarPulsePending || emitted->playerCombat.canActiveSonarPing ||
        emitted->playerCombat.selectedTrackHasEstimatedPosition || !runtime.PlayerActivePulse())
    {
        return false;
    }
    const auto& pulse = *runtime.PlayerActivePulse();
    const float emittedBearing = static_cast<float>(std::atan2(
        static_cast<double>(pulse.forwardUnitVector.y), static_cast<double>(pulse.forwardUnitVector.x)));
    if (std::abs(std::remainder(emittedBearing - selectedBearing, 6.2831853F)) > 0.001F)
    {
        return false;
    }
    physicsWorld.Step(fixedDeltaSeconds);

    // A second edge while the round trip is pending is a normal rejection, not a second pulse.
    commandTimeSeconds += fixedDeltaSeconds;
    const auto repeatedPing = runtime.AdvancePlayerControlled(playerSnapshot, ping, commandTimeSeconds);
    if (!repeatedPing || !repeatedPing->playerCombat.lastCommand ||
        repeatedPing->playerCombat.lastCommand->accepted ||
        !repeatedPing->playerCombat.activeSonarPulsePending || !runtime.PlayerActivePulse())
    {
        return false;
    }
    physicsWorld.Step(fixedDeltaSeconds);

    bool sawQualifiedSpatialTrack = false;
    for (int tick = 0; tick < 240; ++tick)
    {
        commandTimeSeconds += fixedDeltaSeconds;
        const auto frame = runtime.AdvancePlayerControlled(playerSnapshot, {}, commandTimeSeconds);
        if (!frame || runtime.PlayerTorpedo())
        {
            return false;
        }
        for (const auto& track : frame->playerTracks)
        {
            if (track.trackId == selectedTrackId &&
                Weapons::ValidateTrackForWeapon(runtime.PlayerCombat().WeaponDefinition(), track))
            {
                if (!track.estimatedPositionMeters || !track.positionUncertaintyMeters ||
                    commandTimeSeconds - pingEmissionTimeSeconds < 1.5)
                {
                    return false;
                }
                sawQualifiedSpatialTrack = true;
                break;
            }
        }
        physicsWorld.Step(fixedDeltaSeconds);
        if (sawQualifiedSpatialTrack)
        {
            break;
        }
    }
    if (!sawQualifiedSpatialTrack || runtime.PlayerCombat().Weapon().phase != Weapons::WeaponPhase::Stored)
    {
        return false;
    }

    // Fire without readiness remains a normal commander rejection even after a valid ranged solution exists.
    const std::array fire{
        PlayerCombatCommand{.type = PlayerCombatCommandType::FireWeapon}};
    commandTimeSeconds += fixedDeltaSeconds;
    const auto rejectedFire = runtime.AdvancePlayerControlled(playerSnapshot, fire, commandTimeSeconds);
    if (!rejectedFire || !rejectedFire->playerCombat.lastCommand || rejectedFire->playerCombat.lastCommand->accepted ||
        runtime.PlayerTorpedo() || runtime.PlayerCombat().Weapon().targetTrackId)
    {
        return false;
    }
    physicsWorld.Step(fixedDeltaSeconds);

    const std::array prepare{
        PlayerCombatCommand{.type = PlayerCombatCommandType::PrepareWeapon}};
    commandTimeSeconds += fixedDeltaSeconds;
    const auto preparing = runtime.AdvancePlayerControlled(playerSnapshot, prepare, commandTimeSeconds);
    if (!preparing || preparing->playerCombat.weaponPhase != Weapons::WeaponPhase::Preparing ||
        !preparing->playerCombat.selectedTrackId || preparing->playerCombat.canFireWeapon ||
        !preparing->playerCombat.lastCommand || !preparing->playerCombat.lastCommand->accepted ||
        preparing->playerCombat.lastCommand->command != PlayerCombatCommandType::PrepareWeapon || runtime.PlayerTorpedo())
    {
        return false;
    }
    physicsWorld.Step(fixedDeltaSeconds);

    commandTimeSeconds += fixedDeltaSeconds;
    const auto earlyFire = runtime.AdvancePlayerControlled(playerSnapshot, fire, commandTimeSeconds);
    if (!earlyFire || !earlyFire->playerCombat.lastCommand || earlyFire->playerCombat.lastCommand->accepted ||
        earlyFire->playerCombat.weaponPhase != Weapons::WeaponPhase::Preparing ||
        runtime.PlayerCombat().Weapon().targetTrackId || runtime.PlayerTorpedo())
    {
        return false;
    }
    physicsWorld.Step(fixedDeltaSeconds);

    bool sawReady = false;
    for (int tick = 0; tick < 90; ++tick)
    {
        commandTimeSeconds += fixedDeltaSeconds;
        const auto frame = runtime.AdvancePlayerControlled(playerSnapshot, {}, commandTimeSeconds);
        if (!frame || runtime.PlayerTorpedo())
        {
            return false;
        }
        physicsWorld.Step(fixedDeltaSeconds);
        if (frame->playerCombat.weaponPhase == Weapons::WeaponPhase::Ready)
        {
            if (!frame->playerCombat.canFireWeapon || !frame->playerCombat.selectedTrackWeaponQualified)
            {
                return false;
            }
            sawReady = true;
            break;
        }
    }
    if (!sawReady)
    {
        return false;
    }

    commandTimeSeconds += fixedDeltaSeconds;
    const auto launched = runtime.AdvancePlayerControlled(playerSnapshot, fire, commandTimeSeconds);
    if (!launched || !launched->playerCombat.lastCommand || !launched->playerCombat.lastCommand->accepted ||
        launched->playerCombat.lastCommand->command != PlayerCombatCommandType::FireWeapon ||
        launched->playerCombat.weaponPhase != Weapons::WeaponPhase::Launched ||
        !launched->playerCombat.weaponTargetTrackId ||
        launched->playerCombat.weaponTargetTrackId != launched->playerCombat.selectedTrackId ||
        !runtime.PlayerTorpedo() || !runtime.Decoy() || !runtime.Decoy()->active)
    {
        return false;
    }
    if (runtime.PlayerTorpedo()->guidanceTrackId != *launched->playerCombat.selectedTrackId ||
        runtime.PlayerTorpedo()->impactedBody)
    {
        return false;
    }

    return physicsWorld.DestroyBody(runtime.Destroyer().body);
}
} // namespace DeepRun::Tests
'@

# Design/development notes for the accepted control convergence and manual ranging flow.
Replace-Once 'docs/design/controls.md' @'
Keyboard bindings must eventually be rebindable.

---
'@ @'
Keyboard bindings must eventually be rebindable.

M5-J5 implementation note: the combat playground now uses the reference `LT` prepare, `RT` fire, `RB` active
sonar, `Y` contact-select and contextual `X` decoy bindings. In the current tactical-camera context, right-stick
X pans and right-stick Y zooms so the weapon triggers no longer have a conflicting presentation meaning. This is
an M5 context mapping, not a change to the semantic-action boundary or a claim about the final rebindable layout.

---
'@
Replace-Once 'docs/development/m5-combat-playground.md' @'
J3 is additionally accepted at feature commit `3a6fef335878046808e9caa08d3b4df8ceb1bcaf`; post-merge CI
run `34523462484` passed Debug and Release configure, build, CTest, windowed smoke and visual artifacts.
'@ @'
J3 is additionally accepted at feature commit `3a6fef335878046808e9caa08d3b4df8ceb1bcaf`; post-merge CI
run `34523462484` passed Debug and Release configure, build, CTest, windowed smoke and visual artifacts.
J4 is accepted at feature commit `2ac391314f0857d18271368ae716b3fb2144495c`; post-merge CI
run `34546515749` passed Debug and Release configure, build, CTest, windowed smoke and visual artifacts.
'@
Replace-Once 'docs/development/m5-combat-playground.md' @'
The accepted automated smoke path never deploys the player decoy, preserving the deterministic F.2 baseline.

## M5-H.1-B — automated windowed visual acceptance
'@ @'
The accepted automated smoke path never deploys the player decoy, preserving the deterministic F.2 baseline.

### M5-J5 — explicit player active-sonar ranging and D1 control convergence

Status: CANDIDATE — requires the normal Debug/Release CI gate before promotion to the stable M5 branch.

Normal play no longer receives an automatic spatial fire-control solution. The destroyer's continuous acoustic
signature first creates an ordinary bearing-only player Track through `AcousticWorld -> SensorObservation ->
TrackManager`. `ActiveSonarPing` is accepted only for a selected non-lost perceived Track and constructs its beam
from that Track's estimated bearing. Authoritative destroyer position exists only as `AcousticReflector` input to
the simulator. The spatial estimate appears only after the ordinary monostatic round-trip delay and then crosses
back through the same perception boundary. The automated smoke path retains its deterministic auto-ranging helper.

The input layer simultaneously converges the M5 combat mapping on D1: `LT` prepare, `RT` fire, `RB` active sonar,
`Y` contact selection and contextual `X` decoy; keyboard/mouse retains `R`/RMB prepare, LMB fire, Space active
sonar, Tab select and F decoy. Trigger pressure no longer doubles as camera zoom; the current tactical-camera
context uses right-stick Y for continuous controller zoom and right-stick X for pan.

## M5-H.1-B — automated windowed visual acceptance
'@
Replace-Once 'docs/roadmap/milestones.md' @'
- J3 passive-acoustic incoming-threat warning projected to combat UI without hostile position/body truth
'@ @'
- J3 passive-acoustic incoming-threat warning projected to combat UI without hostile position/body truth
- J4 player defensive acoustic decoy with reciprocal seeker diversion through perceived tracks
- J5 explicit player active-sonar ranging from a selected passive Track plus D1 combat-control convergence
'@

# Sanity: no trailing whitespace errors in the staged implementation tree.
git diff --check
