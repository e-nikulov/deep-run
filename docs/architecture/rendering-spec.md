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

Presentation ocean waves do not replace `WaterBody` truth. When CPU wave
queries become authoritative for floating bodies, Simulation owns that query
state and the renderer visualizes a compatible snapshot.

Initial flora is presentation-first. It should be batchable/instanced and must
not create thousands of rigid bodies. Only authored large or gameplay-relevant
flora may opt into coarse collision/query representation.

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
positions are not permanent gameplay identifiers. Gameplay binds to stable
authored nodes, markers, hardpoints, metadata, and definition identifiers.

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
