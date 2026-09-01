# Antey Production Asset Contract

CONTENT STATUS: TECHNICALLY VALIDATED; HUMAN ART APPROVAL PENDING

LICENSE STATUS: SHIPPING BLOCKED PENDING LEGAL REVIEW

Antey is the neutral Project 949A-inspired production asset for the existing
`C0 Player Submarine` package. It is not a new C-ID and contains no historical
boat name, hull number, or commemorative marking.

## Canonical files

| Purpose | Path |
|---|---|
| Immutable artistic source | `Content/submarines/Antey/Source/Antey_Source.blend` |
| Editable production asset | `Content/submarines/Antey/Antey_GameReady.blend` |
| Runtime render asset | `Content/submarines/Antey/Antey.glb` |
| Render metadata | `Content/submarines/Antey/Antey.asset.json` |
| Gameplay authoring | `Content/submarines/Antey/Antey.authoring.json` |
| Reference provenance | `Content/submarines/Antey/References/reference_manifest.md` |
| Fresh-reopen validation | `Content/submarines/Antey/Validation/antey_final_validation.json` |
| P700 cross-fit | `Content/submarines/Antey/Validation/crossfit_final_validation.json` |

The source is retained in the production BLEND only as locked, render-hidden,
non-runtime `REFERENCE_SOURCE` geometry. Production meshes use independent
datablocks and the canonical Blender contract: one unit is one metre, `+X` bow,
`+Y` port, and `+Z` up.

## Exterior and LODs

| Measure | Value |
|---|---:|
| Length | 154.000 m |
| Maximum exterior span, including control planes | 19.000 m |
| Main hull maximum beam | 18.200 m |
| Main hull exterior height | 11.370 m |
| Sail height | 4.400 m |
| Overall exterior height, including raised masts | 19.620 m |

The hull is one closed, longitudinally smoothed station-cage surface with a
complete belly, broad missile region, blunt bow, controlled stern taper, twin
shaft fairings, and large control surfaces. It has no overlapping shoulder
shells or circumferential profile waves. The sail uses a long level crown with
distinct forward and aft transitions. All four masts are vertical. Bow and
tail planes use independent outward trapezoidal surfaces, and all six torpedo
doors are on the upper bow half. The 9.2 m public value is treated as draft,
never as total height. LOD totals are geometry-derived:

| LOD | Objects | Vertices | Triangles |
|---|---:|---:|---:|
| LOD0 | 34 | 37,218 | 74,244 |
| LOD1 | 34 | 29,374 | 58,556 |
| LOD2 | 34 | 14,388 | 28,584 |
| LOD3 | 12 | 4,610 | 9,116 |

The apparent `34 / 74,244` BLEND versus `20 / 69,516` GLB discrepancy was a
classification error, not missing runtime geometry. Complete GLB LOD0
accounting is: 20 base meshes / 69,516 triangles, 12 P700 hatch meshes / 2,256
triangles, 2 propeller meshes / 2,472 triangles, 0 other meshes, for
`TOTAL_RUNTIME_LOD0 = 34 objects / 74,244 triangles`.

`SM_Propeller_Port` and `SM_Propeller_Starboard` are separate real meshes with
seven visible blades, hub-centred origins, unit scale, and local `+X` rotation
axes. They are a mirrored pair; exact historical handedness remains unconfirmed.

## P700 launcher contract

There are 12 P700 launch positions per side. Each side is one dense
longitudinal row of 12 inclined canisters, matching the supplied side cutaway.
Adjacent positions form six paired hatch groups. The port mapping starts with
`PORT_HATCH_01 = HP_P700_PORT_01 + HP_P700_PORT_02` and ends with
`PORT_HATCH_06 = HP_P700_PORT_11 + HP_P700_PORT_12`; starboard is symmetric.

Centres run from `13.175 m` to `36.825 m`, the bank length by centres is
`23.650 m`, and every adjacent spacing is `2.150 m`. Launch direction uses a
40-degree elevation and a 3-degree outward cant as public-reference-guided
authoring approximations. The cross-fit scene instantiates the actual stowed
P700 mesh 24 times using shared mesh datablocks. Minimum launcher-axis distance
is `1.386 m` for a validated maximum stowed missile diameter of `1.262 m`.
The 1.350 m launcher envelope leaves 0.0438 m radial clearance, exceeding the
explicit `minimumRequiredClearance` of 0.0250 m.

## Torpedoes, collision, and authoring

The bow authoring contract contains four `533` markers and two `650` markers.
The two `650` doors form the upper row and the four `533` doors form the denser
row below it; every door and marker remains above the hull centreline. They are
spatial authoring, not weapon simulation. Collision uses only
`COL_Antey_Bow`, `COL_Antey_Main`, `COL_Antey_Aft`, and `COL_Antey_Sail`;
render meshes, propellers, hatches, and small details are excluded.
`PHY_Antey_BuoyancyVolume` is an authoring proxy only.

Ten `VOL_COMP_*` logical volumes prepare future damage/flooding authoring.
Simulation remains authoritative for loading, hatch state, propulsion,
damage, flooding, fire, and crew state.

## Runtime and review boundary

The GLB includes only runtime render nodes and LODs. References, hardpoints,
compartments, collision shapes, buoyancy proxy, cameras, lights, and review
helpers are excluded. The GLB passed import into a factory-empty Blender
process. Its JSON material table contains only `MAT_Antey_Hull` and
`MAT_Antey_Propellers`; `Dots Stroke` and `Material` are factory import-session
defaults created by Blender and are not stored in `Antey.glb`.

The true same-coordinate, same-scale source overlays measure side IoU 0.889 and
top/bottom IoU 0.779. The lower plan-view values are deliberate: the retained
source consists of overlapping, non-manifold multi-component geometry with
abrupt width changes around the missile shoulders and oversized legacy control
planes. Production uses one smooth broad missile region with no overlapping
shoulder shells or circumferential waves, while the public top drawing retains
the intended 949A width distribution. The overlay inspection therefore did not
justify another automatic hull edit; human visual approval remains pending.

Hash-bound bright-clay renders, true source overlays, public drawing
overlays, loaded P700 views, and torpedo diagnostics live under `Review/`.

Technical validation does not constitute final human art approval or legal
shipping approval.
