from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def replace_once(path: Path, old: str, new: str, label: str) -> None:
    text = path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one match, found {count}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8", newline="\n")


cmake = ROOT / "CMakeLists.txt"
replace_once(
    cmake,
    'set(DEEPRUN_ANTEY_RUNTIME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Engine/Assets/submarines/Antey")',
    '# Runtime promotion belongs in the build tree. Never generate/copy production assets into the source tree.\n'
    'set(DEEPRUN_ANTEY_RUNTIME_DIR "${CMAKE_CURRENT_BINARY_DIR}/RuntimeAssets/submarines/Antey")',
    "Antey runtime directory",
)
replace_once(
    cmake,
    'set(DEEPRUN_P700_RUNTIME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Engine/Assets/Weapons/P700")',
    '# Same rule for P-700: Content is canonical; generated runtime staging is build output only.\n'
    'set(DEEPRUN_P700_RUNTIME_DIR "${CMAKE_CURRENT_BINARY_DIR}/RuntimeAssets/Weapons/P700")',
    "P-700 runtime directory",
)
replace_once(
    cmake,
    'add_custom_command(\n    OUTPUT ${DEEPRUN_ANTEY_RUNTIME_FILES}',
    'set(DEEPRUN_ANTEY_RUNTIME_STAMP "${DEEPRUN_ANTEY_RUNTIME_DIR}/.staged")\n'
    'add_custom_command(\n'
    '    OUTPUT "${DEEPRUN_ANTEY_RUNTIME_STAMP}"\n'
    '    BYPRODUCTS ${DEEPRUN_ANTEY_RUNTIME_FILES}',
    "Antey staged output contract",
)
replace_once(
    cmake,
    '        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/StageAnteyRuntime.cmake"\n    DEPENDS',
    '        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/StageAnteyRuntime.cmake"\n'
    '    COMMAND ${CMAKE_COMMAND} -E touch "${DEEPRUN_ANTEY_RUNTIME_STAMP}"\n'
    '    DEPENDS',
    "Antey stage stamp command",
)
replace_once(
    cmake,
    'add_custom_target(DeepRunAnteyRuntimeAssets DEPENDS ${DEEPRUN_ANTEY_RUNTIME_FILES})',
    'add_custom_target(DeepRunAnteyRuntimeAssets DEPENDS "${DEEPRUN_ANTEY_RUNTIME_STAMP}")',
    "Antey runtime target",
)
replace_once(
    cmake,
    'add_custom_command(\n    OUTPUT ${DEEPRUN_P700_RUNTIME_FILES}',
    'set(DEEPRUN_P700_RUNTIME_STAMP "${DEEPRUN_P700_RUNTIME_DIR}/.staged")\n'
    'add_custom_command(\n'
    '    OUTPUT "${DEEPRUN_P700_RUNTIME_STAMP}"\n'
    '    BYPRODUCTS ${DEEPRUN_P700_RUNTIME_FILES}',
    "P-700 staged output contract",
)
replace_once(
    cmake,
    '        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/StageP700Runtime.cmake"\n    DEPENDS',
    '        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/StageP700Runtime.cmake"\n'
    '    COMMAND ${CMAKE_COMMAND} -E touch "${DEEPRUN_P700_RUNTIME_STAMP}"\n'
    '    DEPENDS',
    "P-700 stage stamp command",
)
replace_once(
    cmake,
    'add_custom_target(DeepRunP700RuntimeAssets DEPENDS ${DEEPRUN_P700_RUNTIME_FILES})',
    'add_custom_target(DeepRunP700RuntimeAssets DEPENDS "${DEEPRUN_P700_RUNTIME_STAMP}")',
    "P-700 runtime target",
)

main = ROOT / "DeepRun/Main.cpp"
old_gate = '''                    if (combatRendered->stats.drawCalls < 2U || combatRendered->stats.drawCalls > 20U ||
                        combatRendered->stats.submittedPrimitives != combatRendered->stats.drawCalls ||
                        combatRendered->stats.submittedIndices < 72U)
                    {
                        std::cerr << "[Game][ERROR] M5 combat presentation draw statistics are invalid\\n";
                        return false;
                    }
'''
new_gate = '''                    // CombatPlaygroundView owns asset-specific draw validation. In normal gameplay D2 fog-of-war
                    // may legitimately hide every hostile/civilian presentation before visual classification, so
                    // zero combat draws is a valid frame. Keep only representation-independent accounting here.
                    const bool noCombatDraws = combatRendered->stats.drawCalls == 0U;
                    if (combatRendered->stats.submittedPrimitives != combatRendered->stats.drawCalls ||
                        (noCombatDraws ? combatRendered->stats.submittedIndices != 0U
                                       : combatRendered->stats.submittedIndices == 0U))
                    {
                        std::cerr << "[Game][ERROR] M5 combat presentation draw accounting is invalid: drawCalls="
                                  << combatRendered->stats.drawCalls
                                  << ", submittedPrimitives=" << combatRendered->stats.submittedPrimitives
                                  << ", submittedIndices=" << combatRendered->stats.submittedIndices << '\\n';
                        return false;
                    }
'''
replace_once(main, old_gate, new_gate, "normal FOW presentation draw gate")

print("runtime staging + normal startup draw-gate core fix applied")
