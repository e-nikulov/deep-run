# IG1-D — Production Antey LOD validation / selection policy

Status: IN PROGRESS on `ig1-d`.

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

1. Use the requested LOD if it is available.
2. Otherwise search toward a more detailed available LOD first.
3. Only if no more-detailed variant exists may a less-detailed available variant be selected.
4. Never invent an AssetId.

For the current canonical package this yields:

```text
request LOD0 -> LOD0
request LOD1 -> LOD0 fallback
request LOD2 -> LOD0 fallback
request LOD3 -> LOD0 fallback
```

This preserves geometry correctness while making the current lack of runtime LOD1-LOD3 a visible
performance limitation rather than a hidden content substitution.

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

## Focused coverage

`Tests/IG1DLodPolicyTest.cpp` now exercises the policy in isolation:

- the current LOD0-only package resolves LOD0 directly and LOD1-LOD3 through explicit fallback;
- a future actually-staged requested LOD wins without fallback;
- invalid non-monotonic family metadata is rejected.

The focused test source is intentionally separate while the normal `PhysicalPlayground` path is being
migrated. IG1-D is not complete until that test is wired into CMake/CTest and the normal playground calls
`SelectProductionAnteyRenderAsset` rather than the legacy LOD0-only selector.

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

Before closure also require:

```text
PhysicalPlayground normal path -> SelectProductionAnteyRenderAsset(..., Lod0)
focused IG1-D test -> CMake/CTest
Debug + Release build/CTest
headless + windowed smoke
git diff --check
```
