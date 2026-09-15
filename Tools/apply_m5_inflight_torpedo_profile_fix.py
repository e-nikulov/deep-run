#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PATH = ROOT / "Game/Combat/CombatPlaygroundRuntime.h"
text = PATH.read_text(encoding="utf-8")


def replace_once(old: str, new: str, label: str) -> None:
    global text
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one match, found {count}")
    text = text.replace(old, new, 1)


replace_once(
    """    Weapons::ConventionalTorpedoDefinition playerTorpedoDefinition_;\n    Weapons::P700GranitDefinition playerP700Definition_;\n""",
    """    Weapons::ConventionalTorpedoDefinition playerTorpedoDefinition_;\n    // The selected torpedo profile may change while an already-launched weapon is still running.\n    // Keep the launch-time definition with that projectile so selection cannot mutate its kinematics/identity.\n    std::optional<Weapons::ConventionalTorpedoDefinition> playerTorpedoInFlightDefinition_{};\n    Weapons::P700GranitDefinition playerP700Definition_;\n""",
    "in-flight definition member",
)

replace_once(
    """        // Commit the staged inventory only after the weapon runtime is successfully materialized.\n        playerTorpedoInventory_ = nextTorpedoInventory;\n        playerTorpedo_ = *launched;\n        playerTorpedoLaunchPosition_ = launchPosition;\n""",
    """        // Commit the staged inventory only after the weapon runtime is successfully materialized.\n        playerTorpedoInventory_ = nextTorpedoInventory;\n        playerTorpedoInFlightDefinition_ = playerTorpedoDefinition_;\n        playerTorpedo_ = *launched;\n        playerTorpedoLaunchPosition_ = launchPosition;\n""",
    "capture launch-time torpedo definition",
)

replace_once(
    """        if (playerTorpedo_ && playerTorpedo_->movementDomain == Weapons::MovementDomain::Underwater)\n        {\n            const auto seekerDecision = AdvancePlayerTorpedoSeeker(\n""",
    """        if (playerTorpedo_ && playerTorpedo_->movementDomain == Weapons::MovementDomain::Underwater)\n        {\n            if (!playerTorpedoInFlightDefinition_)\n            {\n                return std::unexpected(\"M5-E.1 live torpedo lost its launch-time definition snapshot\");\n            }\n            const auto seekerDecision = AdvancePlayerTorpedoSeeker(\n""",
    "require launch-time torpedo definition",
)

replace_once(
    """                ? Weapons::AdvanceConventionalTorpedoWithSeekerCueAndCollision(\n                    playerTorpedoDefinition_,\n                    playerTorpedoSeekerConfig_,\n""",
    """                ? Weapons::AdvanceConventionalTorpedoWithSeekerCueAndCollision(\n                    *playerTorpedoInFlightDefinition_,\n                    playerTorpedoSeekerConfig_,\n""",
    "seeker advance uses launch-time definition",
)

replace_once(
    """                : Weapons::AdvanceConventionalTorpedoWithCollision(\n                    playerTorpedoDefinition_, *playerTorpedo_, guidanceTrack, *physicsWorld_, simulationTimeSeconds);\n""",
    """                : Weapons::AdvanceConventionalTorpedoWithCollision(\n                    *playerTorpedoInFlightDefinition_, *playerTorpedo_, guidanceTrack, *physicsWorld_, simulationTimeSeconds);\n""",
    "direct advance uses launch-time definition",
)

replace_once(
    """        if (playerTorpedo_ && playerTorpedo_->movementDomain == Weapons::MovementDomain::Spent &&\n            (playerTorpedo_->terminalReason == Weapons::ConventionalTorpedoTerminalReason::RangeExpired ||\n             playerTorpedo_->terminalReason == Weapons::ConventionalTorpedoTerminalReason::EnduranceExpired))\n        {\n            const auto rearmed = playerCombat_.CompleteResolvedLaunch(simulationTimeSeconds);\n            if (!rearmed)\n                return std::unexpected(\"player torpedo range/endurance re-arm failed: \" + rearmed.error());\n            playerTorpedo_.reset();\n            playerTorpedoLaunchPosition_.reset();\n            playerTorpedoSeekerState_ = Weapons::TorpedoSeekerRuntimeState{\n                .selectedTrackId = std::nullopt,\n                .lastUpdateTimeSeconds = simulationTimeSeconds};\n            pendingPlayerTorpedoSeekerEmissions_.clear();\n            nextPlayerTorpedoSeekerEmissionSampleTimeSeconds_ = simulationTimeSeconds;\n            playerTorpedoActivePulse_.reset();\n            playerTorpedoActiveReflector_.reset();\n            playerTorpedoActivePulseDeadlineSeconds_ = simulationTimeSeconds;\n        }\n""",
    """        if (playerTorpedo_ && playerTorpedo_->movementDomain == Weapons::MovementDomain::Spent)\n        {\n            // Impact, endurance and range expiry all resolve the launched projectile. CompleteResolvedLaunch()\n            // intentionally no-ops when the player already selected/prepared a different weapon.\n            const auto rearmed = playerCombat_.CompleteResolvedLaunch(simulationTimeSeconds);\n            if (!rearmed)\n                return std::unexpected(\"player torpedo resolution re-arm failed: \" + rearmed.error());\n            playerTorpedo_.reset();\n            playerTorpedoInFlightDefinition_.reset();\n            playerTorpedoLaunchPosition_.reset();\n            playerTorpedoSeekerState_ = Weapons::TorpedoSeekerRuntimeState{\n                .selectedTrackId = std::nullopt,\n                .lastUpdateTimeSeconds = simulationTimeSeconds};\n            pendingPlayerTorpedoSeekerEmissions_.clear();\n            nextPlayerTorpedoSeekerEmissionSampleTimeSeconds_ = simulationTimeSeconds;\n            playerTorpedoActivePulse_.reset();\n            playerTorpedoActiveReflector_.reset();\n            playerTorpedoActivePulseDeadlineSeconds_ = simulationTimeSeconds;\n        }\n""",
    "resolve all torpedo terminal states",
)

PATH.write_text(text, encoding="utf-8", newline="\n")
print("M5 in-flight torpedo profile fix applied")
