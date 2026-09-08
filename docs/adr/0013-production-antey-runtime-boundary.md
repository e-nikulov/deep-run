# ADR-0013: Production Antey runtime boundary

## Status

Accepted. IG1 is COMPLETE: IG1-A runtime staging/metadata, IG1-B production
visual replacement, IG1-C collision/buoyancy composition, and IG1-D production
LOD family validation/selection have completed Debug/Release build, CTest, and
smoke acceptance. IG1-B.1 keeps explicitly classified retractable sail devices
stowed in the normal submerged presentation. M4 is READY to begin but remains
NOT STARTED.

## Decision

Before M4, DeepRun will integrate the canonical source-first Antey production
asset through bounded IG1 staging and a production runtime contract:

```text
Content/submarines/Antey/ -> validated production outputs
                           -> IG1 staging / promotion
                           -> Engine/Assets/submarines/Antey/
                           -> runtime submarine representation
                           -> future acoustic systems
```

The runtime representation independently composes motion state, visual asset,
collision representation, buoyancy representation, semantic anchors/metadata,
and submarine physics/gameplay state. Render geometry and render LOD are never
collision or gameplay authority. Collision and buoyancy consume their separate
bounded production/runtime contracts.

```text
ProductionAnteyAssetDefinition
  |- render family / LOD policy
  |- collision proxy contract
  |- buoyancy spatial proxy contract
  |- semantic anchors
  `- retractable sail-device presentation contract
```

IG1-C makes the production collision BOX and production buoyancy spatial BOX
the runtime authorities for their respective representations. The accepted
Game-owned playground mass and effective neutral displacement remain separate
hydrostatic tuning (`mass / water density`) until a later hydrostatics and
submarine-systems contract owns realistic ballast/displacement state. The
buoyancy proxy's geometric volume is therefore not Project 949A displaced
volume and is not used as the current force-volume calibration.

IG1-D treats LOD identity and availability as presentation policy only. The
production family always carries semantic `render.LOD0` through `render.LOD3`
metadata, but the current staged runtime package exposes only the validated
`Antey.glb` LOD0 artifact. Missing variants are not fabricated from metadata.
Staged LOD0 is mandatory. A request selects the requested staged LOD when
available; otherwise it falls back only toward the nearest more-detailed staged
variant, with LOD0 as the guaranteed terminal fallback. A coarser-than-requested
asset is never substituted. With the current package, requests for LOD1--LOD3
therefore resolve explicitly to LOD0. Changing render LOD selection cannot
change collision, buoyancy, mass/displacement tuning, propulsion, controls, or
semantic gameplay anchors.

Runtime consumes semantic records for propellers, torpedo anchors, P-700
launcher data, compartments, collision, buoyancy, and LOD identity. Blender
files, Blender/source hierarchy, GLB node names, and legacy `HP_Antey_*`
markers do not cross this boundary as runtime/gameplay API. The prototype may
remain a test fixture, but is not the normal production/playground visual path
after IG1. The Assets/import layer may resolve a production semantic record
against GLB internals into an opaque presentation binding; Game and Simulation
retain semantic identity, transforms, and authoritative state, never raw GLB
node names.

The normal submerged presentation applies a source-first, geometry-derived
local post-transform only to explicitly `RETRACTABLE` sail-device records.
Their private authoring node references resolve at the Assets boundary to
opaque drawable bindings; `STATIC` sail geometry and the accepted M2/M3
vessel behaviour tuning remain unchanged.

Sail-device deployment metadata is an explicit source-first contract. The
completed sail-envelope audit validates that contract and rejects inconsistent
motion metadata or suspicious raised `STATIC` geometry; it never derives state.

Production semantic anchors must be derived from the authoritative source
metadata or geometry. A zero authoring object origin is not a valid spatial
anchor when the corresponding production geometry is baked in model space.

Authoring-side spatial records cross this boundary once using the C0 basis
conversion `(X, Y, Z) -> (X, Z, -Y)`. Runtime vectors, transforms, and
compartment orientations are consequently in DeepRun coordinates before Game
or Simulation consume them; extent magnitudes map as `(X, Y, Z) -> (X, Z, Y)`.

## Consequences

- IG1 is a bounded post-M3 integration gate, not M4 work, a gameplay milestone,
  or a new M/A/D/C specification axis.
- The Antey LOD0--LOD3 variants share one runtime asset-family identity; LOD
  validation/selection is a bounded Game presentation policy, not a generic
  Engine LOD manager.
- The current lack of staged LOD1--LOD3 is explicit and falls back toward the
  nearest more-detailed available variant; current package requests therefore
  resolve to mandatory staged LOD0. Runtime never invents asset paths.
- Render-LOD selection cannot become authority for collision, buoyancy,
  hydrostatic tuning, or semantic gameplay state.
- M4 receives runtime state and semantic anchors only through its own acoustic
  contract, never through authoring or GLB internals.
- This decision does not implement acoustics, weapons, damage, flooding,
  interiors, cavitation, wake, final materials, screen-space LOD thresholds,
  streaming, cross-fades, or a general-purpose LOD system.
- The Antey legal shipping gate remains independent of technical/runtime
  integration.
