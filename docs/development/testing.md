# Testing DeepRun

Run all commands from the repository root unless a command explicitly changes
directory.

## Configure and build

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
cmake --preset windows-release
cmake --build --preset windows-release
```

## Canonical tests

```powershell
ctest --preset windows-debug
ctest --preset windows-release
```

The registered `DeepRunTests` CTest entry runs:

```text
working directory: build/windows-debug/Debug or build/windows-release/Release
command:           DeepRunTests --asset-root Assets
environment:       no test-specific environment variables
```

`Assets` is relative to the test executable's output directory. CMake stages the required canonical runtime assets into the build output. The current M5 gate registers six CTest targets: `DeepRunTests`, `DeepRunIG1DLodPolicyTests`, `DeepRunM4AcousticTests`, `DeepRunM5WeaponRuntimeTests`, `DeepRunP700ProductionAssetTests`, and `DeepRunP700LauncherInventoryTests`. Milestone acceptance requires all six in both Debug and Release; do not rely on an old single-entry `1/1` assumption.

Do not assume a bare `DeepRunTests.exe` launch is equivalent to CTest. A
supported direct invocation is:

```powershell
Push-Location build/windows-debug/Debug
.\DeepRunTests.exe --asset-root Assets
Pop-Location
```

Launching `build/windows-debug/Debug/DeepRunTests.exe --asset-root Assets`
from the repository root instead searches for `./Assets`, not the copied
build-output assets, and is not an acceptance result.

## Smoke tests

```powershell
.\build\windows-debug\Debug\DeepRun.exe --headless
.\build\windows-release\Release\DeepRun.exe --headless
.\build\windows-debug\Debug\DeepRun.exe --smoke-test
.\build\windows-release\Release\DeepRun.exe --smoke-test
```

`--smoke-test` is the canonical windowed rendering and resize smoke path. In a
Debug build it also enables the existing D3D12 debug validation path; do not
add separate message suppression merely to run this check.

For final M5 acceptance, also run:

```powershell
.\build\windows-debug\Debug\DeepRun.exe --smoke-p700
.\build\windows-release\Release\DeepRun.exe --smoke-p700
```

`--smoke-test` must produce the five combat acceptance checkpoints and machine-readable report. `--smoke-p700` must produce five P-700 state checkpoints plus the mandatory lifecycle markers from hatch opening through terminal and a real `PHYSICAL_IMPACT damage=100`; the report must retain `23/24` launcher inventory and identify the destroyer Jolt body as the physical impact target. Both smoke paths retain BMP/JSON artifact packages in CI. The original technical M5 closure is evidenced by run `34712161730` (#488) on `851a922dd4a2f4055dba523d3dc1a7773dad4ed2`, where Debug and Release passed all six CTest targets and both windowed smokes.

The later M5 torpedo-seeker closure hardening does not add a seventh CTest target or a new smoke command. Its mixed passive/active state machine, delayed active echo, off-beam failure, quiet-target passive failure, decoy seduction, bounded steering, finite endurance and impact terminal-state checks execute inside the existing M5 test composition. Final branch promotion must therefore rerun the same six CTest targets and both windowed smokes in Debug and Release on the clean closure HEAD. The authoritative contract is documented in `m5-torpedo-active-passive-seeker.md`.

For milestone acceptance, report the exact Debug and Release CTest commands
and their results. Treat registered CTest results—not an ad-hoc direct launch
with different working-directory or asset-root inputs—as the canonical signal.

## M3 acceptance performance run

```powershell
.\build\windows-release\Release\DeepRun.exe --benchmark-m3
.\build\windows-release\Release\DeepRun.exe --benchmark-m3 --benchmark-hdr
.\build\windows-debug\Debug\DeepRun.exe --benchmark-m3 --benchmark-stability
.\build\windows-release\Release\DeepRun.exe --benchmark-m3 --benchmark-stability
.\build\windows-debug\Debug\DeepRun.exe --benchmark-m3 --benchmark-hdr --benchmark-stability
.\build\windows-release\Release\DeepRun.exe --benchmark-m3 --benchmark-hdr --benchmark-stability
```

These use the canonical PhysicalPlayground, neutral vessel commands, normal fixed steps,
normal renderer and debug UI, and a borderless 2560x1440 client area (so window borders
do not reduce the render target on a native 1440p display). Requested and actual target
sizes are printed separately. Default output is forced SDR;
`--benchmark-hdr` requests the existing capability-gated scRGB path, with ordinary SDR fallback.
Configured VSync is preserved and its Present interval is logged. Startup model duplication
and window captures are disabled for measurement. Ordinary smoke remains 1280x720 with its
1024x640 resize and capture checks.

The run automatically terminates after 5 seconds of RealTime warm-up plus 25 seconds of
measurement. `--benchmark-stability` requests a 65-second measurement window after the 5-second
warm-up (70 seconds total) and performs four warm-up resizes alternating 1024x640 and 2560x1440.
Measurement samples begin only after the post-resize 2560x1440 baseline is established, so the
exact accepted sample span is logged and may be slightly shorter than 65 seconds. The final
accepted Debug run still exceeded the complete 64-second fish-school wrap. No gameplay clock or
physics tuning is changed.
Do not minimize or resize during measurement. Minimization interrupts the run with an error.
Benchmark flags cannot be combined with headless or ordinary smoke.

Samples are capped at 65,536 per interval. Saturation reports NOT MEASURABLE, without
asserting a machine timing threshold. Percentiles interpolate at `(N-1)*p` after sorting.
The measured intervals are:

- `cpu_full_update_render`: elapsed wall time for Update plus Render, including synchronization;
- `cpu_excluding_present_slot_wait`: that interval less measured Present and frame-slot waits;
  this is main-thread elapsed work, not an OS CPU-cycle measurement;
- `fixed_game_physics`: all fixed Game hooks and PhysicsWorld steps in the frame (including zero-step frames);
- `game_render_submission`: the Game render hook, excluding engine output/UI and Present;
- `present` and `frame_slot_wait`: CPU synchronization intervals;
- `frame_pacing`: successive measured application frame starts, including measurement bookkeeping;
- `gpu_full_frame`: direct-queue timestamps around the scene clear, all scene draws, output pass,
  debug UI and final Present transition, excluding Present/DWM waiting and timestamp resolve.

GPU results are read only after an existing completed frame-slot fence. Source frame numbers
exclude warm-up results and prevent duplicates; the last in-flight results are omitted, so
GPU and pacing sample counts differ from rendered-frame count. No extra per-frame flush or
GPU allocation is introduced. Unavailable GPU timing is NOT MEASURABLE, never a CPU proxy.

Memory logs label process working set/private commitment, DXGI current-process local-segment
usage/budget, logical geometry-buffer bytes, and tracked renderer resource/model counts.
The resource count excludes ImGui-owned resources, descriptor/query heaps and PSOs. It includes
model and field buffers, frame upload buffers, swap-chain/scene/depth targets and timestamp readback.
It is not a complete allocation profiler or a claim of exact game-owned VRAM.

Exit status checks successful completion, existing scene invariants, measurement resolution,
and stable tracked geometry/resource/model counts; machine-dependent performance targets remain
reviewed evidence. Debug runs validate D3D12; Release runs provide performance acceptance evidence.
These commands supplement, and never replace, canonical CTest.

For HDR windowed/resize smoke, run the ordinary `--smoke-test` with `renderer.hdr`
temporarily set to `true` in that executable's built `Config/engine.json`, then restore
the original file. The repository configuration need not change. Check the logged
actual output mode: a request alone is not proof of HDR. Existing BMP captures from
an HDR run are SDR compositor previews for composition/motion review, not HDR-encoded
captures or physical-panel luminance measurements.
