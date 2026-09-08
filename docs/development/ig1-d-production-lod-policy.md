# IG1-D — Production Antey LOD validation / selection policy

Status: IN PROGRESS on `ig1-d`; runtime wiring complete, acceptance validation pending.

## Scope

IG1-D closes the production render-family boundary without introducing a general-purpose LOD system.
The production asset family keeps four semantic metadata records (`LOD0`..`LOD3`) while the current
runtime package exposes only the validated `Antey.glb` LOD0 render artifact.

This distinction is intentional:

```text
source-family metadata: LOD0 LOD1 LOD2 LOD3
runtime-available assets: LOD0 only
```

Missing runtime variants are not fabricated, renamed, copied, or inferred from metadata counts.

## Validation policy

The runtime family is valid when:

- the asset family is exactly `submarine.antey`;
- semantic IDs remain ordered `render.LOD0` through `render.LOD3`;
- every metadata record has positive object/vertex/triangle counts;
- vertex and triangle counts do not increase toward coarser LODs;
- LOD0 always has a staged runtime asset;
- unavailable LODs remain metadata-only rather than fake file paths.

The offline validator additionally parses the staged GLB and checks that actual LOD0 triangle count
stays close to source-family topology metadata. A small exporter/topology accounting tolerance is allowed;
it is not interpreted as permission for arbitrary geometry drift.

## Selection policy

The bounded policy accepts a requested semantic LOD level and resolves only among actually staged assets.
Because staged LOD0 is a mandatory package invariant, fallback is deliberately one-directional:

1. Use the requested LOD if it is available.
2. Otherwise walk toward the nearest more-detailed available LOD.
3. LOD0 is the guaranteed terminal fallback.
4. Never substitute a coarser-than-requested asset.
5. Never invent an AssetId.

For the current canonical package this yields:

```text
request LOD0 -> LOD0
request LOD1 -> LOD0 fallback
request LOD2 -> LOD0 fallback
request LOD3 -> LOD0 fallback
```

If a future package stages LOD2 as well, a request for LOD3 resolves to LOD2 rather than jumping directly
to LOD0. This preserves geometry correctness while making absent runtime variants an explicit performance
limitation rather than a hidden content substitution.

## Runtime boundary

Render LOD selection is presentation policy only. It must not change:

- Jolt collision proxy;
- buoyancy proxy / COB;
- effective displaced-volume tuning;
- mass;
- drag, propulsion, or control-surface state;
- semantic launch/compartment/propeller anchors;
- submerged sail-device state.

Physics and gameplay therefore remain stable if the selected render LOD changes in a future slice.

The normal `PhysicalPlayground` initialization now requests `ProductionRenderLodLevel::Lod0` through
`SelectProductionAnteyRenderAsset` and loads the returned `assetId`. Initialization diagnostics record the
requested level, selected level, fallback state, and selected asset. The old IG1-B LOD0-only selector is
consulted only as a temporary compatibility invariant and is not the selected-asset authority; it must agree
with the new policy until the legacy helper/test expectation is removed during closure cleanup.

## Focused coverage

`Tests/IG1DLodPolicyTest.cpp` exercises the policy in isolation:

- the current LOD0-only package resolves LOD0 directly and LOD1-LOD3 through explicit fallback;
- a future actually-staged requested LOD wins without fallback;
- the nearest more-detailed staged LOD wins when the requested LOD is absent;
- a package missing mandatory staged LOD0 is rejected;
- invalid non-monotonic family metadata is rejected.

The focused executable is registered with CMake/CTest as `DeepRunIG1DLodPolicyTests`. It intentionally has
no renderer, physics, authoring-file, or staged-content dependency; it validates deterministic selection from
an already-loaded semantic production definition.

## Deferred

IG1-D does not implement:

- screen-space error thresholds;
- distance hysteresis;
- cross-fade/dither transitions;
- runtime streaming of absent LOD files;
- meshlets/GPU-driven LOD;
- a reusable engine-wide LOD manager.

Those require real staged LOD variants and representative profiling evidence.

## Validation commands

From repository root:

```powershell
python Tools/Blender/validate_antey_runtime_lods.py --package Content/submarines/Antey
python Tools/Blender/validate_antey_runtime_lods.py --package Engine/Assets/submarines/Antey
```

Both packages must report the same family/availability/selection result.

Runtime/CTest wiring now established:

```text
PhysicalPlayground normal selected-asset authority -> SelectProductionAnteyRenderAsset(..., Lod0)
focused IG1-D policy test -> DeepRunIG1DLodPolicyTests -> CTest
```

Before marking IG1-D or IG1 complete still require:

```text
remove the temporary legacy-selector compatibility invariant and update the old source-scan expectation
Debug + Release configure/build/CTest
headless + windowed smoke
both offline LOD validators
git diff --check
roadmap + ADR closure update
```
