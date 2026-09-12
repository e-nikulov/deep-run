# Antey Production Asset Contract

CONTENT STATUS: TECHNICALLY ACCEPTED; HUMAN ART APPROVAL PASS

LICENSE STATUS: LEGAL_BLOCKED / PENDING_REVIEW

The canonical provenance record classifies the source and derivative production
asset as `NON-SHIPPABLE UNTIL RESOLVED`; see
[`asset-provenance.md`](asset-provenance.md) and
[`third-party/antey-asset-license-review.md`](third-party/antey-asset-license-review.md).
This legal gate is independent of the technical and human-art acceptance
recorded below.

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
| Stern repair regression fixture | `Content/submarines/Antey/selected_geometry.txt` |
| Rudder-boundary QA provenance | `Content/submarines/Antey/manual_rudder_boundary.json` |
| Fresh-reopen validation | `Content/submarines/Antey/Validation/SourceFirst/FinalPromotionQA/canonical_runtime_glb_validation.json` |
| Source accounting | `Content/submarines/Antey/Validation/SourceFirst/FinalPromotionQA/canonical_source_accounting/source_partition_forensic_summary.json` |

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

The BLEND authoring LOD0 and runtime GLB are separate accounting boundaries.
The canonical GLB intentionally exports one selected runtime LOD (LOD0), with
all independently addressable articulated geometry retained. Complete fresh
import accounting is: 40 LOD0 base meshes / 100,159 triangles, 12 P700 cover
meshes / 6,718 triangles, 14 propeller meshes (blades and hubs) / 11,466
triangles, 0 other meshes, for `TOTAL_RUNTIME_LOD0 = 66 objects / 118,343
triangles`. No LOD1-LOD3 draw-set leakage is present.

`SM_Propeller_Port` and `SM_Propeller_Starboard` are separate real meshes with
seven visible blades, hub-centred origins, unit scale, and local `+X` rotation
axes. They are a mirrored pair; exact historical handedness remains unconfirmed.

### IG1-A.3 stern source-exterior regression

`selected_geometry.txt` is a permanent defect-localization and regression
fixture for the stern source-exterior region. It is not a triangle source and
must not be used to reconstruct geometry directly. The
`validate_antey_exterior_continuity.py` validator maps the selected region back
to the authoritative source and checks source coverage and articulation at
neutral, `+/-15` degrees, and `+/-20` degrees.

The accepted repair restores 928 source faces / 2,030 source triangles. The
canonical production BLEND contains 66 LOD0 runtime objects, 65,548 vertices,
64,413 faces, and 120,374 triangles. Fresh GLB import contains 66 mesh nodes
and 120,373 triangles; the one-triangle difference is a zero-area hull polygon
omitted by the GLB export/import path and is not visible source-exterior loss.

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
row below it; every door and marker remains above the hull centreline. This
intentional six-lid layout occupies the upper bow hemisphere and does not
allocate the central axial, predominantly lower/forward bow to torpedo tubes.
They are spatial authoring, not weapon simulation. Collision uses only
`COL_Antey_Bow`, `COL_Antey_Main`, `COL_Antey_Aft`, and `COL_Antey_Sail`;
render meshes, propellers, hatches, and small details are excluded.
`PHY_Antey_BuoyancyVolume` is an authoring proxy only.

### Main bow sonar reserved region

`MGK540_BOW_ARRAY` is the canonical semantic ID for the primary bow acoustic
array of the MGK-540 `Skat-3` complex. `Antey.authoring.json` records it in
`semanticRegions` without a geometric anchor: no bounded antenna geometry is
authored. It reserves the central, predominantly lower/forward bow as a
content region.

The region is a content semantic marker only: it is not a physical collider,
authoritative sonar-simulation state, a requirement to model an internal
antenna, or a runtime implementation of MGK-540. Future torpedo geometry,
internal weapon volumes, and other large bow elements must not intersect this
reserved allocation without an explicit Antey content-contract review. The
accepted `4 x 533 mm + 2 x 650 mm` upper-bow arrangement remains compatible
with this reservation and must be retained by future Antey geometry work.

No bounded antenna geometry, numerical sonar-volume extents, or sonar anchor
marker are authored. Consequently, validation preserves the semantic record and
upper-bow torpedo layout but deliberately has no automatic region-intersection
test: inventing an OBB, radius, or coordinate bounds would create unsupported
geometry data.

Future sonar simulation may support abstract acoustic bands and passive/active
modes, but real-world MGK-540 frequency parameters are not verified content
contract data and are not recorded here.

Ten stable `compartment.01` .. `compartment.10` logical volumes prepare future damage/flooding authoring. Their longitudinal boundaries and functional roles are aligned to the supplied Project 949A longitudinal-section reference through `Antey.compartments.json`. The bulkhead X positions are `REFERENCE_DERIVED_APPROXIMATE` (nominal tolerance about +/-1 m), because the supplied drawing is not dimensioned shipyard documentation. Compartment 03 intentionally remains `THIRD_UNSPECIFIED` rather than inventing a function absent from the supplied legend. P-700 containers, bow sonar allocation, VVD bottles, shafting, steering gear, and sail equipment remain outside the pressure-compartment contract.
Simulation remains authoritative for loading, hatch state, propulsion,
damage, flooding, fire, and crew state.

## Runtime and review boundary

The GLB includes only runtime render nodes for the explicitly selected LOD0.
References, hardpoints,
compartments, collision shapes, buoyancy proxy, cameras, lights, and review
helpers are excluded. The GLB contains no baked runtime animations: the 12
P700 cover nodes remain independently addressable, while
`LauncherSystem.P700CoverState_*` owns their `CLOSED`, `OPENING`, `OPEN`, and
`CLOSING` state. The GLB passed import into a factory-empty Blender process.
Its JSON material table contains only `MAT_Antey_Hull` and
`MAT_Antey_Propellers`; `Dots Stroke` and `Material` are factory import-session
defaults created by Blender and are not stored in `Antey.glb`.

The true same-coordinate, same-scale source overlays measure side IoU 0.889 and
top/bottom IoU 0.779. The lower plan-view values are deliberate: the retained
source consists of overlapping, non-manifold multi-component geometry with
abrupt width changes around the missile shoulders and oversized legacy control
planes. Production uses one smooth broad missile region with no overlapping
shoulder shells or circumferential waves, while the public top drawing retains
the intended 949A width distribution. The overlay inspection therefore did not
justify another automatic hull edit; human visual approval is recorded as
PASS.

Hash-bound bright-clay renders, true source overlays, public drawing
overlays, loaded P700 views, and torpedo diagnostics live under `Review/`.

Technical acceptance is complete and user visual approval is PASS. Legal
shipping approval remains blocked pending review; future WeaponSystem and
Simulation systems remain outside this asset promotion.
