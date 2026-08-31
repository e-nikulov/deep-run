# Antey Canonical Submarine Asset

CONTENT STATUS: FROZEN FOR CURRENT MILESTONE

P700 visual hatch geometry: DEFERRED TO WEAPON CONTENT PASS

LICENSE STATUS: SHIPPING BLOCKED PENDING LEGAL REVIEW

The base Antey asset is frozen for this milestone. Further changes require a
specific gameplay/content requirement; weapon-specific geometry is not a
current-milestone blocker.

The canonical neutral asset is authored under:

```text
Content/submarines/Antey/
```

Runtime files are `Antey.glb` and `Antey.asset.json`. The editable production
file is `Antey_GameReady.blend`; the immutable source copy is under
`Source/Antey_Source.blend`. The supplied source is retained separately and is
not modified by the preparation tool.

## Authoring contract

- Blender units are Metric, scale `1.0`, with one Blender unit equal to one metre.
- Local `+X` is bow/forward, `+Y` is port/left, and `+Z` is up.
- The root is `SM_Submarine_Antey_ROOT` at the approximate geometric centre.
- Render nodes have identity rotation and unit scale. Propeller object origins
  are at their hub centres and their future presentation rotation axis is local `+X`.
- The production material set is `MAT_Antey_Hull` and `MAT_Antey_Propeller`.
- Runtime GLB export contains only the six render mesh nodes: four LOD hull
  nodes and two propellers. Authoring markers and proxies stay in the BLEND and
  metadata sidecar because the current loader does not consume empty nodes.

## Geometry and LODs

The proportion correction pass uses approximately `154 x 18 x 9.2 m` for
length, beam, and overall height. This is an authoring target from the public
side/top references, not a claim that every open-source drawing agrees on one
exact dimensional convention. The source surface was retained after controlled cleanup; seven tiny aft loose
components identified as source propeller geometry were removed and replaced
by two clean runtime propellers. This is not a decimate-only conversion.

Current LOD counts are recorded by the deterministic validation report:

| Node | Triangles | Role |
|---|---:|---|
| `SM_Antey_LOD0` | 102,880 | close/showcase silhouette |
| `SM_Antey_LOD1` | 63,779 | normal gameplay distance |
| `SM_Antey_LOD2` | 28,798 | reduced-distance representation |
| `SM_Antey_LOD3` | 8,744 | distant silhouette |

LOD0 is below the requested 150k–250k target because the cleaned source
already provides the required silhouette at 102,880 triangles; it remains
below the 300k hard maximum and the engine's 50k–150k hero-content guideline.

All four LODs are derived from the same corrected LOD0 mesh, retain matching
world-space bounds, and are checked for consistent component structure and
normals. The wireframe diagnostics remain intentionally non-shipping review
artifacts.

Control surfaces remain integrated in the source render geometry because
extracting them would damage the available topology. Semantic markers are
provided for future control-surface authoring; no animation or runtime control
system is introduced here.

## Gameplay authoring data

The production BLEND contains 24 neutral P-700 attachment markers, eight
neutral torpedo markers, two propeller and wake marker pairs, sonar/reference
markers, ten `VOL_COMP_*` gameplay volumes with ten centre markers, four
collision proxy volumes, and `PHY_Antey_BuoyancyVolume`.

The current P-700 hatch objects are explicitly classified as authoring /
placement proxies: `SM_P700_Hatch_Port_01..06` and
`SM_P700_Hatch_Starboard_01..06`. They remain separate mesh objects, retain
their current transforms, pair with two hardpoints each, keep
`hide_render=True`, and remain excluded from the runtime GLB. Each proxy is
only 12 triangles and is not a final visual hatch asset or an
animation-ready shipping mesh. No proxy geometry is changed in the freeze
pass.

For a future P-700 launch sequence, create separate visual-quality hatch
meshes: six port and six starboard, with the correct exterior silhouette,
closed-state hull fit, and an animation pivot/axis based on a selected public
reference or an explicit gameplay abstraction. The future implementation must
avoid z-fighting or duplicate hull surfaces and define a separate runtime
export contract. The opening mechanism is deliberately not reconstructed now.

`Antey.authoring.json` is an authoring-only sidecar. It records names,
positions, Euler rotations, quaternion orientations, and forward vectors for
P-700 and torpedo hardpoints, propeller origins, sonar markers, bow/stern/
centre markers, and OBB centre/orientation/half-extents for the ten gameplay
compartments.

Open-source references disagree on the exact torpedo-tube configuration;
current authoring choice must cite its reference and remains revisable before
combat implementation.

The ten compartment volumes are gameplay abstractions, not claims about real
watertight bulkheads or authoritative real-world internal geometry. Collision
and buoyancy proxies are authoring inputs only. Rendering never becomes
authoritative gameplay state: simulation will own propulsion, weapon state,
damage, flooding, acoustics, and future compartment state.

No P-700 model, torpedo model, damage system, flooding system, animation
system, acoustics implementation, or combat implementation is included.

## Visual review references and diagnostics

Reference overlays are generated from the public DeepStorm side/top drawing
and compared against the neutral production render. The public Wikimedia side
silhouette is retained as a second visual cross-check. The reference images
are review inputs only and do not become runtime geometry.

The package keeps the existing dark previews and adds `review_side_clay.png`,
`review_top_clay.png`, `review_bow_clay.png`, `review_stern_clay.png`,
`review_wireframe_side.png`, `review_wireframe_top.png`,
`review_p700_hatches.png`, `review_p700_axes.png`,
`review_propeller_axes.png`, `preview_reference_side_overlay.png`, and
`preview_reference_top_overlay.png`.

Reference provenance: [DeepStorm Project 949A page](https://deepstorm.ru/DeepStorm.files/45-92/nsrs/949A/list.htm), [Wikimedia Commons Oscar II silhouette](https://commons.wikimedia.org/wiki/File:Oscar_II_class_SSGN.svg), and the [Bellona Arctic Nuclear Challenge PDF](https://network.bellona.org/content/uploads/sites/3/The_Arctic_Nuclear_Challenge.pdf) used for the dimensional cross-check.

## Validation

Run from the repository root:

```powershell
& "$env:LOCALAPPDATA\Programs\blender\blender.exe" --background --factory-startup --python Tools/Blender/prepare_antey.py -- --correct --source Content/submarines/kursk.blend
```

The tool writes `source_audit.md`, `validation_report.txt`, the GLB, and the
required non-shipping previews. It independently reimports the GLB before
writing the validation result.
