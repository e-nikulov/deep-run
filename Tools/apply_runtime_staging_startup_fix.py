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

ci = ROOT / ".github/workflows/ci.yml"
text = ci.read_text(encoding="utf-8")
build_anchor = '''      - name: Test
        shell: pwsh
        run: ctest --preset ${{ matrix.preset }}
'''
insert = '''      - name: Verify build keeps runtime staging out of source tree
        shell: pwsh
        run: |
          $dirty = @(git status --porcelain -- Engine/Assets)
          if ($dirty.Count -ne 0) {
            Write-Host "Build dirtied Engine/Assets:"
            $dirty | ForEach-Object { Write-Host $_ }
            throw "runtime asset staging must remain inside the build tree"
          }
          foreach ($path in @(
              ".\\build\\${{ matrix.preset }}\\RuntimeAssets\\submarines\\Antey\\.staged",
              ".\\build\\${{ matrix.preset }}\\RuntimeAssets\\Weapons\\P700\\.staged")) {
            if (-not (Test-Path $path)) {
              throw "expected build-tree runtime staging stamp is missing: $path"
            }
          }

      - name: Test
        shell: pwsh
        run: ctest --preset ${{ matrix.preset }}

      - name: Normal gameplay startup smoke
        shell: pwsh
        run: |
          $executable = ".\\build\\${{ matrix.preset }}\\${{ matrix.configuration }}\\DeepRun.exe"
          $stdout = "normal-startup-${{ matrix.preset }}.stdout.log"
          $stderr = "normal-startup-${{ matrix.preset }}.stderr.log"
          Remove-Item $stdout, $stderr -ErrorAction SilentlyContinue
          $process = Start-Process -FilePath $executable -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
          try {
            Start-Sleep -Seconds 6
            $process.Refresh()
            $text = ((Get-Content $stdout -ErrorAction SilentlyContinue) +
                     (Get-Content $stderr -ErrorAction SilentlyContinue)) -join "`n"
            if ($process.HasExited) {
              Write-Host "----- normal startup output -----"
              Write-Host $text
              Write-Host "----- end normal startup output -----"
              throw "DeepRun normal gameplay exited unexpectedly during startup (exit code $($process.ExitCode))"
            }
            if ($text -match "\\[(Game|Render)\\]\\[ERROR\\]") {
              Write-Host "----- normal startup output -----"
              Write-Host $text
              Write-Host "----- end normal startup output -----"
              throw "DeepRun normal gameplay emitted a startup render/game error"
            }
            if ($text -notmatch "Engine initialized in windowed mode" -or
                $text -notmatch "M5 live combat runtime active") {
              throw "DeepRun normal gameplay did not reach the expected initialized combat state"
            }
          }
          finally {
            $process.Refresh()
            if (-not $process.HasExited) {
              Stop-Process -Id $process.Id -Force
            }
          }
'''
count = text.count(build_anchor)
if count != 1:
    raise RuntimeError(f"CI Test anchor: expected exactly one match, found {count}")
ci.write_text(text.replace(build_anchor, insert, 1), encoding="utf-8", newline="\n")

print("runtime staging + normal startup draw-gate fix applied")
