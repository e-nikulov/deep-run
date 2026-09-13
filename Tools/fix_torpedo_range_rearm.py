from pathlib import Path

root = Path(__file__).resolve().parents[1]
path = root / "Game/Combat/CombatPlaygroundRuntime.h"
text = path.read_text(encoding="utf-8")
needle = """            }\n        }\n\n        std::optional<Weapons::P700GranitImpact> p700Impact{};\n"""
replacement = """            }\n        }\n\n        // A miss that consumes propulsion/endurance is a resolved launch just like P-700 RangeExpired.\n        // Keep Impact state resident for the existing visual-acceptance contract, but never leave normal gameplay\n        // permanently stuck in WeaponPhase::Launched after a torpedo simply runs out of range or endurance.\n        if (playerTorpedo_ && playerTorpedo_->movementDomain == Weapons::MovementDomain::Spent &&\n            (playerTorpedo_->terminalReason == Weapons::ConventionalTorpedoTerminalReason::RangeExpired ||\n             playerTorpedo_->terminalReason == Weapons::ConventionalTorpedoTerminalReason::EnduranceExpired))\n        {\n            const auto rearmed = playerCombat_.CompleteResolvedLaunch(simulationTimeSeconds);\n            if (!rearmed)\n                return std::unexpected(\"player torpedo range/endurance re-arm failed: \" + rearmed.error());\n            playerTorpedo_.reset();\n            playerTorpedoLaunchPosition_.reset();\n            playerTorpedoSeekerState_ = Weapons::TorpedoSeekerRuntimeState{\n                .selectedTrackId = std::nullopt,\n                .lastUpdateTimeSeconds = simulationTimeSeconds};\n            pendingPlayerTorpedoSeekerEmissions_.clear();\n            nextPlayerTorpedoSeekerEmissionSampleTimeSeconds_ = simulationTimeSeconds;\n            playerTorpedoActivePulse_.reset();\n            playerTorpedoActiveReflector_.reset();\n            playerTorpedoActivePulseDeadlineSeconds_ = simulationTimeSeconds;\n        }\n\n        std::optional<Weapons::P700GranitImpact> p700Impact{};\n"""
count = text.count(needle)
if count != 1:
    raise RuntimeError(f"expected exactly one player-torpedo/P700 boundary, found {count}")
path.write_text(text.replace(needle, replacement, 1), encoding="utf-8", newline="\n")
print("torpedo range/endurance re-arm patch applied")
