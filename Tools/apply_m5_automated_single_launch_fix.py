#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
runtime_path = root / "Game/Combat/CombatPlaygroundRuntime.h"
test_path = root / "Tests/M5CombatPlaygroundRuntimeChecks.h"
runtime = runtime_path.read_text(encoding="utf-8")
test = test_path.read_text(encoding="utf-8")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)

runtime = replace_once(
    runtime,
    """        if (automatedPlayer)\n        {\n            if (!playerTorpedo_.has_value())\n            {\n                const auto automated = AdvanceAutomatedPlayerCommander(playerSnapshot, simulationTimeSeconds);\n                if (!automated)\n                {\n                    return std::unexpected(automated.error());\n                }\n            }\n        }\n""",
    """        if (automatedPlayer)\n        {\n            // The automated M5 composition is a deterministic one-shot acceptance scenario, not an AI loop.\n            // Once its first player round has been committed, resolving that projectile must not cause an\n            // unintended second launch merely because automatic readiness has re-armed the selected weapon.\n            if (!automatedPlayerLaunchCommitted_ && !playerTorpedo_.has_value())\n            {\n                const auto automated = AdvanceAutomatedPlayerCommander(playerSnapshot, simulationTimeSeconds);\n                if (!automated)\n                {\n                    return std::unexpected(automated.error());\n                }\n            }\n        }\n""",
    "automated one-shot gate",
)

runtime = replace_once(
    runtime,
    """            if (!launch)\n            {\n                return std::unexpected(launch.error());\n            }\n        }\n\n        if (decoy_)\n""",
    """            if (!launch)\n            {\n                return std::unexpected(launch.error());\n            }\n            if (automatedPlayer)\n            {\n                automatedPlayerLaunchCommitted_ = true;\n            }\n        }\n\n        if (decoy_)\n""",
    "mark automated launch committed",
)

runtime = replace_once(
    runtime,
    """        if (playerCombat_.Weapon().phase == Weapons::WeaponPhase::Stored)\n        {\n            const auto prepared = playerCombat_.Execute(\n                {.type = PlayerCombatCommandType::PrepareWeapon}, tracks, simulationTimeSeconds);\n            if (!prepared || !prepared->accepted)\n            {\n                return std::unexpected(\"M5-H automated commander could not prepare the player weapon\");\n            }\n        }\n""",
    """        if (playerCombat_.Weapon().phase == Weapons::WeaponPhase::Stored)\n        {\n            const auto prepared = playerCombat_.BeginAutomaticPreparation(simulationTimeSeconds);\n            if (!prepared)\n            {\n                return std::unexpected(\"M5-H automated commander could not begin automatic player weapon preparation: \" +\n                                       prepared.error());\n            }\n        }\n""",
    "remove explicit automated prepare command",
)

runtime = replace_once(
    runtime,
    """    bool p700AcceptanceMode_ = false;\n    bool playerFogOfWarActive_ = false;\n""",
    """    bool p700AcceptanceMode_ = false;\n    bool automatedPlayerLaunchCommitted_ = false;\n    bool playerFogOfWarActive_ = false;\n""",
    "automated launch member",
)

test = replace_once(
    test,
    """        std::abs(runtime.Destroyer().integrity.remainingIntegrity - 40.0F) > 0.001F ||\n        !runtime.PlayerTorpedo() || runtime.PlayerTorpedo()->movementDomain != Weapons::MovementDomain::Spent ||\n        !runtime.DestroyerTorpedo() || runtime.DestroyerTorpedo()->movementDomain != Weapons::MovementDomain::Spent ||\n""",
    """        std::abs(runtime.Destroyer().integrity.remainingIntegrity - 40.0F) > 0.001F ||\n        runtime.PlayerTorpedo().has_value() ||\n        !runtime.DestroyerTorpedo() || runtime.DestroyerTorpedo()->movementDomain != Weapons::MovementDomain::Spent ||\n""",
    "final resolved player torpedo expectation",
)

runtime_path.write_text(runtime, encoding="utf-8", newline="\n")
test_path.write_text(test, encoding="utf-8", newline="\n")
print("M5 automated single-launch regression fix applied")
