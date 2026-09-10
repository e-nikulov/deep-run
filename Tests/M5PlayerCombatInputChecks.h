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
        static_cast<std::uint16_t>(GamepadButton::A);
    const auto disconnected = SemanticActionsForGamepad(GamepadState{
        .connected = false,
        .leftTrigger = 1.0F,
        .rightTrigger = 1.0F,
        .buttons = combatButtons});
    if (disconnected.selectContact || disconnected.prepareWeapon || disconnected.fireWeapon)
    {
        return false;
    }

    const auto controller = SemanticActionsForGamepad(GamepadState{
        .connected = true,
        .buttons = combatButtons});
    if (!controller.selectContact || !controller.prepareWeapon || !controller.fireWeapon)
    {
        return false;
    }

    const GamepadState triggerOnly{
        .connected = true,
        .leftTrigger = 0.20F,
        .rightTrigger = 0.80F,
        .buttons = 0U};
    const auto triggerActions = SemanticActionsForGamepad(triggerOnly);
    const auto triggerAxes = SemanticAxesForGamepad(triggerOnly);
    if (triggerActions.selectContact || triggerActions.prepareWeapon || triggerActions.fireWeapon ||
        std::abs(triggerAxes.cameraZoom - 0.60F) > 0.001F)
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

    // BeginFrame clears presentation edges but not the monotonic command sequence consumed by fixed-step Game.
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
    const std::array fireKeyDown{
        Platform::WindowEvent{.type = Platform::WindowEventType::KeyDown, .key = Platform::Key::Space}};
    input.ProcessEvents(fireKeyDown);
    if (!input.State().WasPressed(InputAction::FireWeapon) ||
        !input.State().IsDown(InputAction::FireWeapon) || input.State().IsDown(InputAction::PrepareWeapon) ||
        input.State().PressSequence(InputAction::FireWeapon) == 0U)
    {
        return false;
    }
    const std::array fireKeyUp{
        Platform::WindowEvent{.type = Platform::WindowEventType::KeyUp, .key = Platform::Key::Space}};
    input.ProcessEvents(fireKeyUp);

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
