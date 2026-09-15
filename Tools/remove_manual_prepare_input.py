#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
files = {
    "state": root / "Engine/Input/InputState.h",
    "header": root / "Engine/Input/InputSystem.h",
    "cpp": root / "Engine/Input/InputSystem.cpp",
    "test": root / "Tests/M5PlayerCombatInputChecks.h",
}
texts = {name: path.read_text(encoding="utf-8") for name, path in files.items()}


def rep(name: str, old: str, new: str, label: str) -> None:
    text = texts[name]
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, found {count}")
    texts[name] = text.replace(old, new, 1)

rep("state", "    PrepareWeapon,\n", "", "remove prepare input action")
rep(
    "header",
    "// right-stick X to horizontal pan and right-stick Y to continuous zoom; LT/RT remain weapon actions.\n",
    "// right-stick X to horizontal pan and right-stick Y to continuous zoom; RT fires and LT is unbound.\n",
    "controller contract comment",
)
rep("header", "    bool prepareWeapon = false;\n", "", "remove controller prepare semantic")
rep("header", "    bool prepareWeaponKeyDown_ = false;\n", "", "remove prepare key state")

rep(
    "cpp",
    """    // M5-J5 gives LT/RT back to the canonical weapon semantics. In the current tactical-camera context,\n    // right-stick X pans and right-stick Y supplies controller zoom so camera control remains controller-complete.\n""",
    """    // In the current tactical-camera context, right-stick X pans and right-stick Y supplies controller zoom.\n    // Weapon preparation is automatic; RT remains fire and LT is intentionally not a weapon-preparation action.\n""",
    "axes comment",
)
rep(
    "cpp",
    """    const float leftTrigger = std::isfinite(gamepad.leftTrigger)\n                                  ? std::clamp(gamepad.leftTrigger, 0.0F, 1.0F)\n                                  : 0.0F;\n""",
    "",
    "remove left trigger prepare mapping",
)
rep("cpp", "        .prepareWeapon = leftTrigger >= TriggerActionThreshold,\n", "", "remove controller prepare initializer")
rep(
    "cpp",
    """            else if (event.key == Platform::Key::R)\n            {\n                prepareWeaponKeyDown_ = true;\n            }\n""",
    "",
    "remove R key down prepare",
)
rep(
    "cpp",
    """            else if (event.key == Platform::Key::R)\n            {\n                prepareWeaponKeyDown_ = false;\n            }\n""",
    "",
    "remove R key up prepare",
)
rep(
    "cpp",
    """    state_.SetActionDown(\n        InputAction::PrepareWeapon,\n        prepareWeaponKeyDown_ ||\n            state_.IsMouseButtonDown(static_cast<std::size_t>(Platform::MouseButton::Right)) ||\n            controller.prepareWeapon);\n""",
    "",
    "remove semantic prepare refresh",
)

rep("test", "        disconnected.nextWeapon || disconnected.prepareWeapon || disconnected.fireWeapon ||\n",
    "        disconnected.nextWeapon || disconnected.fireWeapon ||\n", "disconnected prepare assertion")
rep("test", "        !controller.nextWeapon || !controller.prepareWeapon || !controller.fireWeapon ||\n",
    "        !controller.nextWeapon || !controller.fireWeapon ||\n", "controller prepare assertion")
rep("test", "    if (legacyFaceButtons.turnAround || legacyFaceButtons.prepareWeapon || legacyFaceButtons.fireWeapon ||\n",
    "    if (legacyFaceButtons.turnAround || legacyFaceButtons.fireWeapon ||\n", "legacy prepare assertion")
rep("test", "        triggerActions.nextWeapon || triggerActions.prepareWeapon || !triggerActions.fireWeapon ||\n",
    "        triggerActions.nextWeapon || !triggerActions.fireWeapon ||\n", "trigger prepare assertion")
rep(
    "test",
    """    input.BeginFrame();\n    const std::array prepareKeyDown{\n        Platform::WindowEvent{.type = Platform::WindowEventType::KeyDown, .key = Platform::Key::R}};\n    input.ProcessEvents(prepareKeyDown);\n    if (!input.State().WasPressed(InputAction::PrepareWeapon) ||\n        !input.State().IsDown(InputAction::PrepareWeapon) || input.State().IsDown(InputAction::FireWeapon) ||\n        input.State().PressSequence(InputAction::PrepareWeapon) == 0U)\n    {\n        return false;\n    }\n    const std::array prepareKeyUp{\n        Platform::WindowEvent{.type = Platform::WindowEventType::KeyUp, .key = Platform::Key::R}};\n    input.ProcessEvents(prepareKeyUp);\n\n""",
    "",
    "remove keyboard prepare test",
)
rep("test", "        !input.State().IsDown(InputAction::DeployDecoy) || input.State().IsDown(InputAction::FireWeapon) ||\n        input.State().IsDown(InputAction::PrepareWeapon) ||\n",
    "        !input.State().IsDown(InputAction::DeployDecoy) || input.State().IsDown(InputAction::FireWeapon) ||\n", "remove decoy prepare assertion")
rep(
    "test",
    """    input.BeginFrame();\n    const std::array prepareMouseDown{\n        Platform::WindowEvent{\n            .type = Platform::WindowEventType::MouseButtonDown,\n            .mouseButton = Platform::MouseButton::Right}};\n    input.ProcessEvents(prepareMouseDown);\n    if (!input.State().WasPressed(InputAction::PrepareWeapon) ||\n        !input.State().IsDown(InputAction::PrepareWeapon))\n    {\n        return false;\n    }\n\n    input.BeginFrame();\n    const std::array fireMouseDown{\n        Platform::WindowEvent{\n            .type = Platform::WindowEventType::MouseButtonDown,\n            .mouseButton = Platform::MouseButton::Left}};\n    input.ProcessEvents(fireMouseDown);\n    if (!input.State().WasPressed(InputAction::FireWeapon) ||\n        !input.State().IsDown(InputAction::FireWeapon) ||\n        !input.State().IsDown(InputAction::PrepareWeapon))\n    {\n        return false;\n    }\n\n    input.BeginFrame();\n    const std::array releaseMouse{\n        Platform::WindowEvent{\n            .type = Platform::WindowEventType::MouseButtonUp,\n            .mouseButton = Platform::MouseButton::Right},\n        Platform::WindowEvent{\n            .type = Platform::WindowEventType::MouseButtonUp,\n            .mouseButton = Platform::MouseButton::Left}};\n    input.ProcessEvents(releaseMouse);\n    if (input.State().IsDown(InputAction::PrepareWeapon) ||\n        input.State().IsDown(InputAction::FireWeapon) ||\n        !input.State().WasReleased(InputAction::PrepareWeapon) ||\n        !input.State().WasReleased(InputAction::FireWeapon))\n    {\n        return false;\n    }\n\n""",
    """    // Manual preparation was removed from player input. RMB is therefore free and must not synthesize fire.\n    input.BeginFrame();\n    const std::array rightMouseDown{\n        Platform::WindowEvent{\n            .type = Platform::WindowEventType::MouseButtonDown,\n            .mouseButton = Platform::MouseButton::Right}};\n    input.ProcessEvents(rightMouseDown);\n    if (input.State().IsDown(InputAction::FireWeapon))\n    {\n        return false;\n    }\n    const std::array rightMouseUp{\n        Platform::WindowEvent{\n            .type = Platform::WindowEventType::MouseButtonUp,\n            .mouseButton = Platform::MouseButton::Right}};\n    input.ProcessEvents(rightMouseUp);\n\n    input.BeginFrame();\n    const std::array fireMouseDown{\n        Platform::WindowEvent{\n            .type = Platform::WindowEventType::MouseButtonDown,\n            .mouseButton = Platform::MouseButton::Left}};\n    input.ProcessEvents(fireMouseDown);\n    if (!input.State().WasPressed(InputAction::FireWeapon) ||\n        !input.State().IsDown(InputAction::FireWeapon))\n    {\n        return false;\n    }\n\n    input.BeginFrame();\n    const std::array fireMouseUp{\n        Platform::WindowEvent{\n            .type = Platform::WindowEventType::MouseButtonUp,\n            .mouseButton = Platform::MouseButton::Left}};\n    input.ProcessEvents(fireMouseUp);\n    if (input.State().IsDown(InputAction::FireWeapon) ||\n        !input.State().WasReleased(InputAction::FireWeapon))\n    {\n        return false;\n    }\n\n""",
    "replace mouse prepare test",
)

for name, path in files.items():
    path.write_text(texts[name], encoding="utf-8", newline="\n")
print("manual prepare input removed")
