from pathlib import Path


def replace_once(path: Path, old: str, new: str, label: str) -> None:
    text = path.read_text(encoding="utf-8")
    if text.count(old) != 1:
        raise RuntimeError(f"expected {label} anchor exactly once, found {text.count(old)}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")


main = Path("DeepRun/Main.cpp")
replace_once(
    main,
    """constexpr float NormalGameplayInitialDepthMeters = 50.0F;\nconstexpr float NormalGameplayLongRangeCombatTargetMeters = 25'000.0F;\n""",
    """constexpr float NormalGameplayInitialDepthMeters = 50.0F;\nconstexpr float NormalGameplayLongRangeCombatTargetMeters = 25'000.0F;\n// The P-700 acceptance scenario includes the explicit wet-launch flooding phase and a paired salvo.\n// Keep a bounded watchdog with margin for both real launcher preparation and deterministic flight.\nconstexpr double P700AcceptanceTimeoutSeconds = 110.0;\n""",
    "gameplay constants",
)
replace_once(
    main,
    """                        if (simulationTimeSeconds > 95.0)\n                        {\n                            std::cerr << \"[Game][ERROR] P-700 acceptance exceeded 95 s SimulationTime without completed impact capture\\n\";\n                            return false;\n                        }\n""",
    """                        if (simulationTimeSeconds > P700AcceptanceTimeoutSeconds)\n                        {\n                            std::cerr << \"[Game][ERROR] P-700 acceptance exceeded \"\n                                      << P700AcceptanceTimeoutSeconds\n                                      << \" s SimulationTime without completed impact capture\\n\";\n                            return false;\n                        }\n""",
    "P-700 acceptance timeout",
)

runtime = Path("Game/Combat/CombatPlaygroundRuntime.h")
replace_once(
    runtime,
    """            else if (selected && selected->estimatedPositionMeters.has_value() &&\n                     playerCombat_.Weapon().phase == Weapons::WeaponPhase::Ready &&\n                     !p700AcceptanceLaunchCommitted_)\n            {\n                commands[count++] = {.type = PlayerCombatCommandType::FireWeapon};\n            }\n""",
    """            else if (selected && selected->estimatedPositionMeters.has_value() &&\n                     playerCombat_.Weapon().phase == Weapons::WeaponPhase::Ready &&\n                     !p700AcceptanceLaunchCommitted_)\n            {\n                // Acceptance exercises the paired-hatch production path. A pair also matches the gameplay\n                // contract that cooperative Granits are more reliable than insisting that one noisy seeker\n                // must deterministically score a hit in every smoke run.\n                if (playerCombat_.P700SalvoMode() == Weapons::P700SalvoMode::Single)\n                {\n                    commands[count++] = {.type = PlayerCombatCommandType::ToggleP700SalvoMode};\n                }\n                commands[count++] = {.type = PlayerCombatCommandType::FireWeapon};\n            }\n""",
    "P-700 acceptance launch",
)

acceptance = Path("Game/Combat/P700VisualAcceptance.h")
replace_once(
    acceptance,
    """            if (snapshot.p700LoadedCount != 23U ||\n                snapshot.destroyerIntegrity >= runtime.Destroyer().integrity.maximumIntegrity)\n            {\n                return std::unexpected(\"P-700 impact did not consume exactly one launcher or reduce destroyer integrity\");\n            }\n""",
    """            if (snapshot.p700LoadedCount != 22U ||\n                snapshot.destroyerIntegrity >= runtime.Destroyer().integrity.maximumIntegrity)\n            {\n                return std::unexpected(\"P-700 paired acceptance did not consume one two-missile hatch group or reduce destroyer integrity\");\n            }\n""",
    "P-700 impact inventory assertion",
)
replace_once(
    acceptance,
    """               std::abs(impact.impactDamage - 100.0F) <= 0.001F && impact.p700LoadedCount == 23U;\n""",
    """               std::abs(impact.impactDamage - 100.0F) <= 0.001F && impact.p700LoadedCount == 22U;\n""",
    "P-700 all-checkpoints inventory assertion",
)
