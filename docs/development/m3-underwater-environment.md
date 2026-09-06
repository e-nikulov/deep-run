# M3-I underwater environment acceptance evidence

Date: 2026-09-06. Base under test: `5b5f353 game: add M3-H.1 presentation fish school`,
with the accepted M3-I instrumentation and documentation changes. A through I are ACCEPTED.
Milestone 3 — Underwater Environment is COMPLETE. M4 has not started.

## Scope note

The vessel used by the accepted M3 performance scene is the M2 prototype
`submarines/prototype/submarine_prototype.glb`, not the C0 source-first Antey production asset.
M3-I therefore accepts the underwater environment/runtime stack and reference-PC environment
workload; it is not production-vessel content-performance evidence. Antey runtime integration and
its production-content benchmark remain separate follow-up work.

## Console and display context

- Active local console session: `Evgeniy`, session 1; no Remote Desktop presentation path used for
  the results below.
- CPU: AMD Ryzen 5 5600X 6-Core Processor; RAM: 32 GiB.
- GPU selected by D3D12: AMD Radeon RX 6600, driver `32.0.21043.19003`, 8,146 MiB dedicated
  memory.
- OS: Windows 11 Pro, version `10.0.26200`, build 26200.
- Native output: 2560x1440 at 144 Hz. The executable verifies its actual render-target extent in
  every benchmark report.
- HDR capability reported by the active output: `IDXGIOutput6` available, HDR active, 10 bits per
  color, luminance min/max/full-frame `0.0099/430/430` nits. With `--benchmark-hdr` it selected
  `R16G16B16A16_FLOAT` and `RGB_FULL_G10_NONE_P709` (scRGB). This is functional output-path
  evidence; it does not assert that the panel itself presents PQ.

The prior RDP measurements and HDR fallback record are superseded for acceptance by this local
console run.

## Method and instrumentation

The executable runs the accepted scene at 2560x1440 with configured VSync and normal 60 Hz fixed
simulation. Five seconds of monotonic-clock warm-up exclude initialization and resize work. The
primary run samples 25 seconds; stability mode requests a 65-second measurement window, with
accepted sampling beginning only after the post-resize native-resolution baseline is established.
Neutral vessel commands are used; no content, effect, resolution, VSync, or physics setting is
reduced for the run.

The M3-I path records bounded CPU vectors (65,536 samples maximum), scene/physics fixed work,
game render submission, Present, frame-slot waits and frame pacing. GPU time comes from two
fence-protected timestamp pairs in a persistent four-query heap and 32-byte readback resource. It
brackets renderer work from clears through scene, output, ImGui and final Present transition;
Present, the resolve and compositor waits are excluded. Percentiles use sorted linear interpolation
at `(N - 1) * p`. A saturated vector reports `NOT MEASURABLE`.

Process working set/private commitment use `PROCESS_MEMORY_COUNTERS_EX`; adapter-local process
usage and budget use `IDXGIAdapter3::QueryVideoMemoryInfo`. Geometry, model and known-resource
counts come from the renderer. The resize-stability baseline is established only after the queued
warm-up resize has returned to 2560x1440; this prevents a transient target-size change from being
misreported as a resource-lifetime regression.

## Primary Release results

Both runs completed with exit code 0, actual 2560x1440 output, structural contract PASS and sample
capacity PASS. Values are milliseconds.

| Metric | SDR median / p95 / p99 / max | HDR scRGB median / p95 / p99 / max |
|---|---:|---:|
| Complete Update + Render | 6.9475 / 7.0204 / 7.1199 / 7.2490 | 6.9470 / 7.0152 / 7.0905 / 7.2963 |
| CPU excluding Present and slot wait | 0.0901 / 0.1887 / 0.2205 / 0.3749 | 0.0905 / 0.1912 / 0.2267 / 0.4894 |
| Fixed Game hooks + physics | 0.0001 / 0.0741 / 0.0851 / 0.1706 | 0.0001 / 0.0765 / 0.0875 / 0.1171 |
| Game render submission | 0.0147 / 0.0211 / 0.0312 / 0.0574 | 0.0153 / 0.0216 / 0.0349 / 0.2075 |
| Present | 6.8341 / 6.9233 / 6.9978 / 7.1388 | 6.8342 / 6.9189 / 6.9810 / 7.1487 |
| Frame-slot wait | 0.0002 / 0.0003 / 0.0003 / 0.0009 | 0.0001 / 0.0003 / 0.0003 / 0.0009 |
| Frame pacing | 6.9501 / 7.0234 / 7.1232 / 7.2515 | 6.9496 / 7.0177 / 7.0931 / 7.3058 |
| Whole-frame GPU execution | 0.6268 / 0.6284 / 0.6298 / 0.6323 | 0.6500 / 0.6517 / 0.6537 / 0.6678 |

Each primary run recorded 3,600 frame samples and 3,599 pacing intervals over 24.9997 seconds.
Median-equivalent pacing was 143.8828 FPS (SDR) and 143.8932 FPS (HDR). The p99 pacing results
are below the 16.67 ms sustained-60-FPS gate, while CPU critical work and GPU execution are well
below their 8–10 ms and 12–13 ms engineering budgets.

## Stability, resize and memory

`--benchmark-stability` performs four 1024x640 / 2560x1440 transitions during warm-up, returns to
native resolution before the baseline, then requests a 65-second measurement window after the
native-resolution baseline is re-established. Debug HDR scRGB completed 9,206 frames (64.3030-second
sampled span) with structural and capacity PASS; its p99 pacing was
7.1581 ms, CPU excluding synchronization 0.6467 ms and GPU 0.6545 ms. D3D12 validation was clean
at shutdown. Release HDR scRGB completed 9,137 frames (64.9987-second sampled span), also with
structural and capacity PASS; p99 pacing was 7.1557 ms, CPU excluding synchronization 0.2360 ms
and GPU 0.6536 ms. The isolated maximum wall-time pauses (Debug 319.7 ms; Release 1,146.3 ms) did
not affect the sustained p99 result and are reported rather than hidden.

During the Release HDR stability run, working set remained 106,287,104–106,835,968 bytes and
private commitment 150,822,912–150,925,312 bytes. DXGI local process usage settled at
115,367,936 bytes against a 7,736,619,008-byte OS budget. Logical geometry stayed 180,264 bytes,
with 31 tracked renderer resources and six models at every measured snapshot. The Debug HDR run
had the same constant logical counts and 115,437,568-byte settled DXGI usage. This is bounded
runtime evidence, not a claim that no driver or allocator leak is possible in arbitrary future
content.

The primary Release SDR run used 86,532,096 bytes of local process memory; HDR used 115,367,936
bytes. The increase is expected from the HDR scRGB swap chain and size-dependent targets. Both
remain far below the 5.5–6 GB engine/game steady-state VRAM working target on the 8 GB reference
RX 6600. DXGI current-process local-segment usage is not exact game-owned residency.

## Scene, authority and lifetime audit

The scene contract remained 12 draws, 10 model primitives, 10,980 submitted indices and 3,660
known submitted triangles. Its 59 static bodies are 54 terrain columns, three collidable rocks and
two ice boxes; submarine and float are the two dynamic bodies. Flora, fauna and particles create
no bodies. The existing 100 m submarine reference depth, buoyancy, drag, propulsion, control
surfaces, XY/rotation-Z constraints and wave-aware float path remain unchanged.

The established authority paths remain intact: semantic input flows to Game commands, marine
calculations, `PhysicsWorld`/Jolt, body state and rendering; waves flow from the CPU wave sample to
the renderer snapshot; `SimulationTime` drives wave/float behaviour; `PresentationTime` drives
particles and fish. Timing diagnostics do not feed gameplay. M3-I did not add Simulation/Game
D3D12 dependencies or change physics tuning.

Six GPU models upload at startup and retain 20 default-heap geometry buffers. Gerstner and
particle buffers are created once; two mapped scene-presentation buffers, timestamp resources and
their readback survive resize. Four swap-chain/scene/depth resources are recreated only on resize
after the existing flush. The float now reuses one cached immutable base draw and updates only pose
and normal transforms, avoiding the prior per-render override/draw-vector allocation. The audit
does not make a global zero-allocation claim: pre-existing submarine draw work, diagnostics and
ImGui can allocate.

## Validation and visual review

| Gate | Result |
|---|---|
| Configure and build, Debug and Release | PASS |
| `ctest --test-dir build/windows-debug -C Debug --output-on-failure` | PASS, registered 1/1; internal 249/249 |
| `ctest --test-dir build/windows-release -C Release --output-on-failure` | PASS, registered 1/1; internal 249/249 |
| Debug and Release `DeepRun.exe --headless` | PASS in the final pass |
| Debug and Release `DeepRun.exe --smoke-test` | PASS in the final pass; ordinary resize completed |
| Debug D3D12 HDR smoke | PASS: uploads, field creation, rendering, HDR scRGB output, resize and shutdown validation clean |
| Release SDR and HDR primary performance | PASS: native 2560x1440, structural/capacity PASS, exit 0 |
| Debug and Release HDR resize stability | PASS: native final resolution, structural/capacity PASS, exit 0; Debug validation clean |
| SDR and HDR qualitative review on the active display | PASS: user confirmed both are readable |
| `git diff --check` | PASS: no whitespace errors |

GDI BMP captures under `build/m3-i-evidence/` are SDR previews and cannot encode HDR. They still
show the coherent water, terrain, flora, ice, fish school, submarine, float and readable ImGui.
The HDR output-path claim above comes from the active output capability and swap-chain log, while
the panel readability result comes from the local visual review.

## Acceptance decision

| Budget / gate | Verdict |
|---|---|
| Sustained 16.67 ms frame budget; recurring p99 below about 20 ms | PASS: SDR p99 7.1232 ms; HDR p99 7.0931 ms |
| GPU engineering target <=12–13 ms | PASS: SDR p99 0.6298 ms; HDR p99 0.6537 ms |
| CPU critical work <=8–10 ms | PASS: SDR p99 0.2205 ms; HDR p99 0.2267 ms |
| Fewer than 1,500 normal scene draws | PASS: 12 |
| 2–3 million normal visible triangles | PASS by established scene accounting: 3,660 submitted triangles; this is an upper bound, not an occlusion query |
| Local-console reference-PC performance acceptance | PASS: AMD Ryzen 5 5600X / Radeon RX 6600 at 2560x1440, active console, SDR and HDR scRGB |

M3-I: ACCEPTED. Milestone 3 — Underwater Environment: COMPLETE. M4: NOT STARTED. Raw generated
logs remain under `build/m3-i-*.log` and are working-copy evidence, not production assets.
