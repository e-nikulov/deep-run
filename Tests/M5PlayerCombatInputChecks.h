#pragma once

#include "Engine/Diagnostics/Logger.h"
#include "Engine/Input/InputSystem.h"
#include "Game/Submarine/AnteyHandlingModel.h"
#include "Simulation/Marine/PropulsionSystem.h"

#include <array>
#include <cmath>
#include <cstdint>

namespace DeepRun::Tests
{
[[nodiscard]] inline bool RunM5PlayerCombatInputChecks()
{
    using namespace Input;

    const std::uint16_t combatButtons =
        static_cast<std::uint16_t>(GamepadButton::LeftStick) |
        static_cast<std::uint16_t>(GamepadButton::Y) |
        static_cast<std::uint16_t>(GamepadButton::DpadLeft) |
        static_cast<std::uint16_t>(GamepadButton::DpadRight) |
        static_cast<std::uint16_t>(GamepadButton::X) |
        static_cast<std::uint16_t>(GamepadButton::RightShoulder);
    const auto disconnected = SemanticActionsForGamepad(GamepadState{
        .connected = false,
        .leftTrigger = 1.0F,
        .rightTrigger = 1.0F,
        .buttons = combatButtons});
    if (disconnected.turnAround || disconnected.selectContact || disconnected.previousWeapon ||
        disconnected.nextWeapon || disconnected.prepareWeapon || disconnected.fireWeapon ||
        disconnected.activeSonarPing || disconnected.deployDecoy)
    {
        return false;
    }

    const auto controller = SemanticActionsForGamepad(GamepadState{
        .connected = true,
        .leftTrigger = 0.80F,
        .rightTrigger = 0.90F,
        .buttons = combatButtons});
    if (!controller.turnAround || !controller.selectContact || !controller.previousWeapon ||
        !controller.nextWeapon || !controller.prepareWeapon || !controller.fireWeapon ||
        !controller.activeSonarPing || !controller.deployDecoy)
    {
        return false;
    }

    // A/B are no longer J2 weapon shortcuts: D1 reserves them for interact/cancel contexts.
    const auto legacyFaceButtons = SemanticActionsForGamepad(GamepadState{
        .connected = true,
        .buttons = static_cast<std::uint16_t>(GamepadButton::A) |
                   static_cast<std::uint16_t>(GamepadButton::B)});
    if (legacyFaceButtons.turnAround || legacyFaceButtons.prepareWeapon || legacyFaceButtons.fireWeapon ||
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
    if (triggerActions.turnAround || triggerActions.selectContact || triggerActions.previousWeapon ||
        triggerActions.nextWeapon || triggerActions.prepareWeapon || !triggerActions.fireWeapon ||
        triggerActions.activeSonarPing || triggerActions.deployDecoy ||
        triggerAxes.cameraPanY != 0.0F || triggerAxes.cameraZoom <= 0.0F)
    {
        return false;
    }

    Diagnostics::Logger logger;
    InputSystem input(logger, false);

    input.BeginFrame();
    const std::array turnDown{
        Platform::WindowEvent{.type = Platform::WindowEventType::KeyDown, .key = Platform::Key::T}};
    input.ProcessEvents(turnDown);
    if (!input.State().WasPressed(InputAction::TurnAround) ||
        input.State().PressSequence(InputAction::TurnAround) == 0U)
    {
        return false;
    }
    const std::array turnUp{
        Platform::WindowEvent{.type = Platform::WindowEventType::KeyUp, .key = Platform::Key::T}};
    input.ProcessEvents(turnUp);

    input.BeginFrame();
    const std::array keyboardFireDown{
        Platform::WindowEvent{.type = Platform::WindowEventType::KeyDown, .key = Platform::Key::Enter}};
    input.ProcessEvents(keyboardFireDown);
    if (!input.State().WasPressed(InputAction::FireWeapon) ||
        input.State().PressSequence(InputAction::FireWeapon) == 0U)
    {
        return false;
    }
    const std::array keyboardFireUp{
        Platform::WindowEvent{.type = Platform::WindowEventType::KeyUp, .key = Platform::Key::Enter}};
    input.ProcessEvents(keyboardFireUp);

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
    const std::array previousWeaponDown{
        Platform::WindowEvent{.type = Platform::WindowEventType::KeyDown, .key = Platform::Key::Z}};
    input.ProcessEvents(previousWeaponDown);
    if (!input.State().WasPressed(InputAction::PreviousWeapon) ||
        input.State().PressSequence(InputAction::PreviousWeapon) == 0U ||
        input.State().IsDown(InputAction::NextWeapon))
    {
        return false;
    }
    const std::array previousWeaponUp{
        Platform::WindowEvent{.type = Platform::WindowEventType::KeyUp, .key = Platform::Key::Z}};
    input.ProcessEvents(previousWeaponUp);

    input.BeginFrame();
    const std::array nextWeaponDown{
        Platform::WindowEvent{.type = Platform::WindowEventType::KeyDown, .key = Platform::Key::C}};
    input.ProcessEvents(nextWeaponDown);
    if (!input.State().WasPressed(InputAction::NextWeapon) ||
        input.State().PressSequence(InputAction::NextWeapon) == 0U ||
        input.State().IsDown(InputAction::PreviousWeapon))
    {
        return false;
    }
    const std::array nextWeaponUp{
        Platform::WindowEvent{.type = Platform::WindowEventType::KeyUp, .key = Platform::Key::C}};
    input.ProcessEvents(nextWeaponUp);

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

    // Canonical Antey handling contract: 24,000 t submerged gameplay mass, weaker astern drive, physical
    // shaft braking through zero, and a non-instantaneous 180-degree 2.5D facing transition.
    using namespace Game::Submarine;
    if (AnteyCanonicalFullSubmergedMassKg != 24'000'000.0F ||
        AnteyGameplayPropulsion.maxReverseRpm >= AnteyGameplayPropulsion.maxForwardRpm ||
        AnteyGameplayPropulsion.maxReverseThrustNewtons >= AnteyGameplayPropulsion.maxForwardThrustNewtons)
    {
        return false;
    }
    const auto braking = Marine::PropulsionSystem::Advance(
        AnteyGameplayPropulsion,
        Marine::PropulsionState{.shaftRpm = 120.0F},
        Marine::PropulsionCommand{.requestedDriveFraction = -1.0F, .availablePowerFraction = 1.0F},
        1.0F);
    if (!braking || braking->nextState.shaftRpm <= 0.0F || braking->nextState.shaftRpm >= 120.0F ||
        braking->thrustNewtons <= 0.0F)
    {
        return false;
    }

    const auto halfTurn = AdvanceAnteyFacing({}, true, AnteyTurnAroundDurationSeconds * 0.5F);
    if (!halfTurn || !halfTurn->turnRequestAccepted || !halfTurn->nextState.turningAround ||
        std::abs(halfTurn->nextState.turnProgress - 0.5F) > 1.0e-4F ||
        std::abs(AnteyLongitudinalForwardProjection(halfTurn->nextState)) > 1.0e-3F)
    {
        return false;
    }
    const auto completedTurn = AdvanceAnteyFacing(
        halfTurn->nextState, false, AnteyTurnAroundDurationSeconds * 0.5F);
    if (!completedTurn || completedTurn->nextState.turningAround ||
        completedTurn->nextState.longitudinalSign != -1 ||
        AnteyLongitudinalForwardProjection(completedTurn->nextState) != -1.0F)
    {
        return false;
    }
    const auto facingTransform = BuildAnteyFacingPresentationTransform(completedTurn->nextState);
    if (std::abs(facingTransform.values[0] + 1.0F) > 1.0e-4F ||
        std::abs(facingTransform.values[10] + 1.0F) > 1.0e-4F)
    {
        return false;
    }

    return true;
}
} // namespace DeepRun::Tests