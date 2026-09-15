from pathlib import Path

path = Path("DeepRun/Main.cpp")
text = path.read_text(encoding="utf-8")

old_constants = """constexpr float NormalGameplayInitialDepthMeters = 50.0F;\nconstexpr float NormalGameplayLongRangeCombatTargetMeters = 25'000.0F;\n"""
new_constants = """constexpr float NormalGameplayInitialDepthMeters = 50.0F;\nconstexpr float NormalGameplayLongRangeCombatTargetMeters = 25'000.0F;\n// The P-700 acceptance scenario now includes the explicit wet-launch flooding phase. Keep a bounded\n// watchdog while diagnosing the production flight path after that preparation-stage change.\nconstexpr double P700AcceptanceTimeoutSeconds = 110.0;\n"""
if text.count(old_constants) != 1:
    raise RuntimeError("expected gameplay constants anchor exactly once")
text = text.replace(old_constants, new_constants, 1)

old_timeout = """                        if (simulationTimeSeconds > 95.0)\n                        {\n                            std::cerr << \"[Game][ERROR] P-700 acceptance exceeded 95 s SimulationTime without completed impact capture\\n\";\n                            return false;\n                        }\n"""
new_timeout = """                        if (simulationTimeSeconds > P700AcceptanceTimeoutSeconds)\n                        {\n                            std::cerr << \"[Game][ERROR] P-700 acceptance exceeded \"\n                                      << P700AcceptanceTimeoutSeconds\n                                      << \" s SimulationTime without completed impact capture\\n\";\n                            if (runtime.PlayerP700().has_value())\n                            {\n                                const auto& missile = *runtime.PlayerP700();\n                                std::cerr << \"[Game][P700][DIAG] phase=\" << static_cast<int>(missile.phase)\n                                          << \" pos=(\" << missile.positionMeters.x << \",\"\n                                          << missile.positionMeters.y << \",\" << missile.positionMeters.z << \")\"\n                                          << \" heading=\" << missile.headingRadians\n                                          << \" travelled=\" << missile.travelledDistanceMeters;\n                                if (missile.perceivedAimPointMeters.has_value())\n                                {\n                                    std::cerr << \" aim=(\" << missile.perceivedAimPointMeters->x << \",\"\n                                              << missile.perceivedAimPointMeters->y << \",\"\n                                              << missile.perceivedAimPointMeters->z << \")\";\n                                }\n                                std::cerr << '\\n';\n                            }\n                            return false;\n                        }\n"""
if text.count(old_timeout) != 1:
    raise RuntimeError("expected P-700 acceptance timeout anchor exactly once")
text = text.replace(old_timeout, new_timeout, 1)
path.write_text(text, encoding="utf-8")
