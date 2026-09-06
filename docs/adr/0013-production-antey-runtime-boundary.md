# ADR-0013: Production Antey runtime boundary

## Status

Accepted for post-M3 Integration Gate IG1; implementation not started

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

Runtime consumes semantic records for propellers, torpedo anchors, P-700
launcher data, compartments, collision, buoyancy, and LOD identity. Blender
files, Blender/source hierarchy, GLB node names, and legacy `HP_Antey_*`
markers do not cross this boundary as runtime/gameplay API. The prototype may
remain a test fixture, but is not the normal production/playground visual path
after IG1. The Assets/import layer may resolve a production semantic record
against GLB internals into an opaque presentation binding; Game and Simulation
retain semantic identity, transforms, and authoritative state, never raw GLB
node names.

## Consequences

- IG1 is a bounded post-M3 integration gate, not M4 work, a gameplay milestone,
  or a new M/A/D/C specification axis.
- The Antey LOD0--LOD3 variants share one runtime asset-family identity;
  selection remains presentation policy.
- M4 receives runtime state and semantic anchors only through its own acoustic
  contract, never through authoring or GLB internals.
- This decision does not implement acoustics, weapons, damage, flooding,
  interiors, cavitation, wake, final materials, or a general-purpose LOD
  system.
- The Antey legal shipping gate remains independent of technical/runtime
  integration.
