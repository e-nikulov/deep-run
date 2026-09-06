# DeepRun Rendering — Technical Specification v0.1

Status: Accepted architecture contract

Specification: A0 — Engine Architecture Baseline (specialized rendering contract)

The project-wide M/A/D/C taxonomy and specification registry are defined in
`docs/README.md`. This document specializes A0; it does not introduce another
A-ID or expand a milestone by itself.

The Direct3D 12 decision, renderer capability tiers, performance budgets, and
render/simulation separation remain canonical in
`docs/architecture/engine-spec.md` and ADR-0004. This document defines the
presentation contracts needed by the next environment work.

<!-- deeprun-rendering-presentation-contract:start -->

## Rendering ownership

The renderer consumes immutable frame snapshots derived from authoritative
simulation state. Rendering may select visibility, LOD, exposure, tone mapping,
particles, animation, and other presentation detail. It never decides physics,
water depth, sonar detection, contact existence, AI knowledge, weapon state, or
damage.

The classic indexed D3D12 vertex/pixel-shader path remains the required
compatibility path. HDR does not require ray tracing, mesh shaders, Work Graphs,
or a renderer rewrite. Optional modern paths remain benchmark- and
capability-gated as defined in A0.

## Scene-linear HDR and display output

Milestone 3 establishes the bounded HDR/output foundation needed by the
underwater environment. The intended pipeline is:

```text
authored material/light values
    -> scene-linear HDR lighting
    -> HDR intermediate render target(s)
    -> exposure and tone mapping
    -> HDR-aware UI composition
    -> SDR or HDR display encoding
    -> present / capture
```

Renderer-owned display capabilities and settings include:

- display/OS HDR capability detection;
- SDR output as the safe, always-supported fallback;
- HDR output only when the active display path supports it;
- paper-white and peak-luminance/output-mapping concepts;
- exposure and tone-map controls;
- HDR-aware UI luminance so UI remains readable without becoming the scene
  light source;
- explicit swap-chain/output colour-space selection.

Enabling or disabling HDR is presentation-only. It must not change simulation
visibility, sensor observations, contacts, tracks, AI, physics, weapon
knowledge, or damage.

M3 need not deliver every production HDR calibration control or final art
grade. It must establish a scene-linear intermediate and a deterministic SDR
fallback without turning the milestone into a complete renderer rewrite.

### Windows M3-A.1 scRGB presentation boundary

On Windows, `IDXGIOutput6::GetDesc1` reporting
`RGB_FULL_G2084_NONE_P2020` with an appropriate bit depth identifies a current
HDR-active Windows output path relevant to DXGI presentation. It is not a claim
that the physical panel itself "is PQ". When capability checks permit HDR
presentation, DeepRun writes linear FP16 scRGB to an
`R16G16B16A16_FLOAT` swap chain tagged `RGB_FULL_G10_NONE_P709`.

Windows/DWM performs the required Advanced Color composition/output conversion
from that application scRGB presentation space toward the active HDR display
path. DeepRun does not own or assume the final physical-link/display encoding.
HDR10/PQ application output, HDR10 metadata, calibration, and final shipping
HDR UI composition remain separate deferred work.

## Screenshot and capture contract

Captures must identify which representation they contain:

- an SDR screenshot is tone-mapped and encoded for ordinary SDR viewing;
- an HDR screenshot/capture preserves the declared HDR colour space and
  luminance metadata in a format that supports them;
- debug captures may preserve scene-linear values but must be labelled as
  diagnostic data rather than ordinary screenshots.

The renderer must not silently save scene-linear/HDR values into an SDR image
whose metadata describes ordinary display-referred content.

## Underwater environment presentation

M3 may introduce the rendering foundation for:

- seabed and large terrain formations;
- rocks, ridges, cliffs, drop-offs, and trenches;
- underwater ice, ice shelves, and icebergs where appropriate;
- the ocean/water-surface boundary;
- underwater fog, depth lighting, particles, and Gerstner surface presentation;
- minimal instanced/batched flora such as kelp, seaweed, seagrass, and benthic
  growth;
- cheap presentation fauna such as fish schools, surface birds, or distant
  ambient movement when useful to the environment slice.

Render geometry is one consumer of world/environment data. Coarse physics,
navigation, and acoustic-query representations are independent consumers and
need not share render-mesh topology. Environment data should use stable authored
identifiers and chunk-compatible bounds so later streaming can be introduced
without making production streaming an M3 requirement.

M3-B.2's representative section uses explicit ordered side-view profile knots
for its ridge, escarpment/drop-off, and trench, plus a bounded list of
Game-owned rock instances with stable local IDs. The classic indexed path
consumes the resulting terrain and combined rock primitives. Coarse static-box
collision samples the authored profile and selected large rocks independently;
it never reads render triangles, GPU handles, or visual tessellation. This is
not a terrain engine, streaming format, or general scene graph.

M3-C applies a compact scene-linear depth-lighting policy in the model pixel
shader before `SceneColorHDR` is tone-mapped. Game derives a finite value
snapshot from authoritative `WaterBody::Config().surfaceLevelY`; the renderer
does not receive `WaterBody`, a camera position, or an environment object. At
each model world position it evaluates `depth = max(surfaceLevelY - worldY, 0)`
and per-channel direct transmission `exp(-k * depth)`. The initial fixed
coefficients in reciprocal metres are RGB `(0.012, 0.006, 0.003)`, so red
attenuates before green and blue. A restrained material-modulated scene-linear
deep ambient RGB floor `(0.02, 0.075, 0.12)` is weighted by
`1 - transmission`; direct diffuse and specular are both multiplied by the
same transmission. This is fixed presentation tuning, not fog, automatic
exposure, an optical-water simulation, or a new gameplay/environment
authority.

M3-C.1 applies a separate view-path fog policy in the same model pixel shader,
after M3-C depth lighting and before `SceneColorHDR` output conversion. Game
supplies the orthographic camera-plane center and view direction with the
existing authoritative surface snapshot; the renderer still receives neither
`WaterBody` nor simulation visibility. For each fragment `F`, with plane center
`C` and normalized view direction `D`, the shader reconstructs `t = dot(F-C,D)`
and ray origin `R = F - D*t`; only the finite orthographic view ray `R -> F`
is analytically clipped below the flat surface plane. This prevents screen X/Y
offset from creating false center-distance fog in the current `-Z` side view.
The shader uses scalar transmission `exp(-0.004 * pathLength)` and blends
toward the existing scene-linear underwater clear RGB `(0.00309598, 0.03954624,
0.11953843)`. An 80-byte scene-presentation payload lives in one persistently
mapped, 256-byte-aligned upload buffer per in-flight frame and is bound as a
root CBV (model root cost: 58 DWORDs: 56 draw constants plus a 2-DWORD root
descriptor). One snapshot is accepted per frame and cannot be overwritten by a
later model draw. This is orthographic view-dependent extinction, not a
depth-light replacement, perspective-fog contract, fullscreen post-process,
volumetric technique, or gameplay visibility system.

M3-D adds one fixed 256-particle suspended-particulate field for the canonical orthographic side view. Game
supplies a finite seed and bounded presentation description only; the renderer performs fixed-seed procedural
layout generation once, owns the resulting two persistent geometry buffers (1,024 XY-quad vertices and 1,536
indices), and draws that field once after opaque terrain and vessel draws into `SceneColorHDR`. Its dedicated
small root signature contains 28 DWORDs of particle/camera constants and a 2-DWORD b1 root-CBV descriptor
that binds the same immutable M3-C.1 scene-presentation payload as opaque models. The transparent PSO enables
alpha blending, retains the existing depth test, and disables depth writes, so particles behind opaque geometry
are rejected while nearby specks blend approximately. The shader applies a small particle-specific depth/fog
attenuation using the shared snapshot; it does not duplicate the opaque material model or create conflicting
water coefficients. Motion is analytic lateral sway and vertically wrapped drift driven by Engine's elapsed
frame clock as `PresentationTime`, never fixed `SimulationTime`; no per-frame heap allocation, GPU allocation,
spawning, or deletion occurs. The field survives swap-chain resize because its GPU resources are independent of
the size-dependent scene/depth targets. This is not arbitrary transparent sorting, a general particle engine,
physics, sonar/acoustic state, bubbles, sediment physics, or volumetric rendering.

M3-E adds one bounded 2.5D Gerstner surface presentation pass before opaque terrain, vessel, and M3-D particle
draws. Marine now owns three fixed components, copied by Game (amplitude/wavelength/angular-frequency/phase/steepness:
`1.75 m / 100 m / 0.28 rad s^-1 / 0.20 rad / 0.55`,
`0.80 m / 45 m / 0.48 rad s^-1 / 1.40 rad / 0.40`, and
`0.35 m / 20 m / 0.82 rad s^-1 / 2.30 rad / 0.20`). Their maximum summed vertical amplitude is `2.90 m`;
the conservative horizontal-slope bound is below `0.5`, which prevents a folded visual profile. The immutable
base mesh has 257 horizontal samples over X `[-340, 340]`, two vertices per sample, and a fixed bottom at
Y `-600`: 514 vertices, 1,536 indices, 512 triangles, and one draw. The vertex shader analytically applies
the three-component displacement from explicit fixed-step `SimulationTime` supplied by Game (M3-E.1
supersedes M3-E's `PresentationTime` input); no CPU mesh update or per-frame allocation is used.
The dedicated opaque backdrop PSO disables depth testing and depth writes, so
later terrain and vessel geometry naturally draws over it. Its deep fill begins with the accepted M2 linear
underwater clear RGB `(0.00309598, 0.03954624, 0.11953843)` and blends only a narrow restrained linear
surface tint RGB `(0.0065, 0.075, 0.18)` at the moving edge.

The renderer is a consumer, never wave authority. `WaterBody::Config().surfaceLevelY` remains the mean/reference
simulation level; `WaterBody::Sample()`, its signed depth, buoyancy, hydro drag, collision,
sonar/acoustics, gameplay visibility, M3-C depth lighting, and M3-C.1 fog remain unchanged and do not use an
instantaneous visual crest or trough. The M3-E visual mesh replaces only the canonical M2 rectangular
underwater clear below the retained full-viewport above-water clear.

M3-E.1 adds optional Marine-owned `WaterWaveFieldDefinition` to `WaterBody`. The explicit
`SampleWaveSurface(worldPosition, simulationTimeSeconds)` returns local Y, signed local depth and an upward
normal. It inverts displaced X with 48 fixed double-precision bisection iterations in `worldX +/- sum(Q*A)`;
the normal is normalized `(-dY/du, dX/du, 0)`. Disabled waves reproduce the flat query. No existing physical
consumer switches to this API. Game's `BuildGerstnerSurfacePresentation` copies components/reference level;
only mesh bounds/colors remain Game tuning. Engine accumulates completed fixed-step seconds and Game passes
that value to `DrawGerstnerSurface`; particles retain `PresentationTime`. No GPU readback, synchronization,
mesh regeneration or new draw is introduced. CPU-query/Render-helper parity allows 0.0001 m for float
publication/phase constants over the tested mesh/time domain; it is not a cross-device bitwise guarantee.
M3-F adds exactly one small Game-owned opaque representative float: a 24-vertex, 36-index, one-primitive
high-visibility box marker rendered through the existing indexed-model path after the submarine and before
particles. Its local origin maps directly to the rigid-body origin; its marker geometry extends above that
origin solely to remain legible at the fixed 600 m side-view span. Its transform comes only from its post-step
`PhysicsWorld::GetBodyState`, never directly from the wave evaluator.
The float is one M3-F physical consumer of `SampleWaveSurface` through the explicit wave-aware buoyancy call;
the canonical submarine remains flat/reference-plane based. The additional draw produces scene totals of
9 draws, 7 model primitives and 9,084 submitted indices. No shader, PSO, mesh regeneration, GPU readback,
fluid velocity, current, waterline clipping, or general floating-object system is introduced. See
[ADR-0011](../adr/0011-authoritative-wave-query.md) and [ADR-0012](../adr/0012-opt-in-wave-buoyancy.md).

M3-G implements the initial flora as one fixed Game-owned presentation field, never as an ecosystem or
gameplay authority. It samples the same authored `SeabedProfileConfig` through `SampleSeabedProfileY`, placing
60 deterministic plants in three bounded patches with roots lifted 0.02 m above the terrain. Four opaque
segmented-ribbon quads per plant become one dark-green material, one primitive, one immutable model upload,
and one draw: 960 vertices, 1,440 indices, and 480 triangles. The field uses the existing indexed-model
shader/PSO, so its opaque fragments receive M3-C depth lighting and M3-C.1 view-path fog/depth behavior without
a shader or PSO change. It renders after terrain and before the submarine, M3-F float, and particles, bringing
the canonical scene totals to 10 draws, 8 model primitives, and 10,524 submitted indices. It creates no
`WaterBody` query, wave authority, Simulation state, physics body, collision, force, entity, animation,
spawning, instancing framework, or per-frame geometry work.

M3-H adds one fixed Game-owned authored ice field as a separate environment representation. Its three stable
formations — surface shelf, hanging formation, and iceberg keel — share one opaque faceted model material,
primitive, immutable upload, and draw (126 vertices, 240 indices, 80 triangles). Game passes only a plain
mean/reference surface Y for static composition; the field has no wave sample, presentation/simulation-time
input, motion, buoyancy, or render-to-environment feedback. Two authored coarse static boxes for the shelf and
keel are independent consumers of the same records, not bounds derived from render vertices. The existing
indexed-model shader/PSO supplies M3-C depth lighting and M3-C.1 fog/depth behavior. Ice renders after flora
and before the submarine, M3-F float, and particles, bringing the canonical scene totals to 11 draws, 9 model
primitives, and 10,764 submitted indices.

M3-H.1 adds one bounded Game-owned presentation fish school through the same opaque indexed-model path. Its
24 fish are deterministic local low-poly geometry (168 vertices, 216 indices, 72 triangles) in one material,
primitive, immutable upload, and draw. Game evaluates one school translation from the Engine's existing
`PresentationTime` each frame: a bounded horizontal wrap and slow vertical oscillation keep the mid-water field
moving without per-fish runtime transforms, geometry regeneration, or allocations. The field does not query
water or participate in gameplay, physics, entities, acoustics, or sensor state. It renders after ice and before
the submarine, M3-F float, and particles, bringing the canonical scene totals to 12 draws, 10 model primitives,
and 10,980 submitted indices. This is presentation policy only.

M3-I adds bounded acceptance diagnostics without changing any scene pass or authority boundary.
One renderer-owned four-entry timestamp query heap and one 32-byte readback buffer cover two in-flight
frames. Each pair brackets the direct command list's scene clear through output/debug UI and the final
Present transition; timestamp resolve and Present/DWM waiting are outside the measured GPU interval.
Readback uses the existing completed frame-slot fence, never an extra per-frame flush. Frame serials
exclude warm-up samples. Resize flushes as before and discards pending timestamp samples while retaining
the query/readback resources. Optional timestamp failure leaves ordinary rendering available.
Renderer memory diagnostics distinguish logical geometry bytes and tracked resources from DXGI's
current-process local-segment usage/budget. These are not exact game-owned residency figures.
See [M3 acceptance evidence](../development/m3-underwater-environment.md) and
[repeatable commands](../development/testing.md#m3-acceptance-performance-run).

Presentation fauna does not create sensor truth. Gameplay-relevant biological
emissions and contacts enter through the A1 signature/observation/contact/track
pipeline.

## Production asset representation boundary

For production vessels and weapons:

```text
authoritative simulation state
    != render/presentation model
    != deliberate physics collision/query representation
```

Detailed render meshes are not Jolt collision meshes by default. Raw vertex
positions and GLB node names are not permanent gameplay identifiers. Gameplay
binds only to stable semantic records, metadata, and definition identifiers;
the Assets/import layer may use production GLB internals once to resolve an
authored node, marker, or hardpoint into an opaque runtime render binding or
model-node index before data crosses the runtime boundary:

```text
production semantic record + GLB/import internals
    -> Assets layer resolves
    -> opaque runtime render binding / model node index
    -> presentation
```

Game and Simulation retain semantic identity, transforms, and authoritative
state only; they neither retain nor look up raw GLB node names. Production
sidecar node references remain permitted private production/import details.

Source/high-detail Blender data, production LOD0, LOD1, LOD2, optional LOD3 or
impostors, baked material/normal assets, collision proxies, gameplay markers,
and provenance may evolve independently when their contracts remain stable.
Do not create a production Asset Cooker until a measured content/runtime need
requires one, and do not replace A0's benchmark-derived performance budgets
with a permanent arbitrary triangle limit.

The accepted Antey and P700 contracts are defined in:

- `docs/content/antey-asset.md`;
- `docs/content/p700-asset.md`;
- `docs/content/asset-pipeline.md`.

This rendering contract does not rename their nodes, markers, hardpoints,
states, pivots, animations, materials, or files.

## Headless boundary

Authoritative world, water, AI, acoustics, weapons, and damage simulation must
remain runnable without a renderer. HDR capability, selected display output,
visual LOD, flora instances, particles, and presentation fauna are not inputs
to deterministic simulation.

<!-- deeprun-rendering-presentation-contract:end -->
