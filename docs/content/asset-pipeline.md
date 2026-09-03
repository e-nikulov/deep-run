# DeepRun C0 Asset Pipeline

## Directory responsibilities

| Path | Responsibility |
|---|---|
| `docs/content/` | Content-package specifications and the source-to-runtime pipeline contract |
| `Content/` | Editable/source assets and authored data |
| `Engine/Assets/` | Runtime asset code plus runtime-ready assets in asset-specific subdirectories |

Do not create a parallel top-level `assets/` directory.

## C0 coordinate and transform contract

- `1` engine unit is `1` metre.
- Blender uses Metric units with Unit Scale `1.0`.
- DeepRun world space is right-handed: `+X` right, `+Y` up, and `+Z` toward the camera.
- For the C0 submarine, local `+X` is forward toward the bow.
- Blender authoring uses local `+X` forward, local `+Z` up, and local `+Y` across the hull.
- The glTF exporter runs with Y-up conversion enabled. It maps Blender `(X, Y, Z)` to glTF `(X, Z, -Y)`.
- The resulting GLB uses local `+X` forward and local `+Y` up, matching the DeepRun M2 gameplay plane without a corrective node rotation.
- Runtime `+Z` remains the DeepRun camera/depth axis; Blender `-Y` maps to runtime `+Z`.
- The asset origin is at the hull geometric centre / approximate prototype centre of mass.
- Hull, sail, and control-surface mesh nodes have identity translation, rotation, and scale.
- The separate propeller node has its expected stern translation because its object origin is at the hub centre; its rotation remains identity and its scale remains `1,1,1`.
- The propeller local rotation axis is `+X`. Any future rotation is presentation-only and must not become authoritative propulsion state.
- Physical dimensions are stored in vertices, not object scale.

The generator validates exactly four expected mesh objects/nodes, Blender-space
bounds, exported GLB position bounds, and the propeller hub origin / local `+X`
rotation contract. It also rejects unexpected runtime node transforms, missing
normals/materials, animations, cameras, lights, and mesh compression extensions.

## Naming

- Mesh objects use the `SM_` prefix and descriptive names.
- Materials use the `M_` prefix.
- Generated filenames use lower snake case.
- Blender default names such as `Cube`, `Cube.001`, and `Material.001` are not allowed.

## Source and runtime formats

The canonical editable source is:

```text
Content/submarines/prototype/submarine_prototype.blend
```

The canonical runtime mesh is binary glTF 2.0:

```text
Engine/Assets/submarines/prototype/submarine_prototype.glb
```

GLB is the canonical runtime mesh format. C0 does not use external textures,
animations, Draco, meshopt, or other optional compression extensions.

## Regeneration

From the repository root, use Blender through its CLI/Python API:

```powershell
blender --background --factory-startup --python Tools/Blender/create_submarine_prototype.py
```

If Blender is not on `PATH`, use the discovered executable explicitly:

```powershell
& "$env:LOCALAPPDATA\Programs\blender\blender.exe" --background --factory-startup --python Tools/Blender/create_submarine_prototype.py
```

The script clears the Blender scene, creates the complete prototype from
Python, validates it, saves the `.blend`, exports the `.glb`, then validates the
GLB structure. Manual `.blend` edits are not part of the reproducible pipeline.

## Antey production preparation

Antey is the canonical production asset inside the existing C0 Player
Submarine package. It is not a second C-ID or a separate registry package.
The canonical neutral Antey package is authored alongside the C0 prototype
source:

```text
Content/submarines/Antey/Antey_GameReady.blend
Content/submarines/Antey/Antey.glb
Content/submarines/Antey/Antey.asset.json
Content/submarines/Antey/Antey.authoring.json
```

The clean pipeline reads the immutable source, creates independent production
mesh datablocks, writes a temporary BLEND, exits Blender, and validates that
saved file in a new process. Only a passing temporary artifact is promoted.
The canonical BLEND is then reopened again for hash-bound geometry validation,
bright-clay reviews, source/public overlays, P700 cross-fit, and GLB export.

Run the pass with:

```powershell
& "$env:LOCALAPPDATA\Programs\blender\blender.exe" --background --factory-startup --python Tools/Blender/build_antey_lod0.py -- --source Content/submarines/Antey/Source/Antey_Source.blend --output Content/submarines/Antey/Antey_GameReady.tmp.blend
```

`Antey.authoring.json` records 24 launcher transforms as two longitudinal
banks of 12, six adjacent-pair hatch groups per side, 4x533 and 2x650 torpedo
markers, propeller origins, compartment OBBs, collision proxies, and the
buoyancy authoring volume. These are not authoritative gameplay state.

P700 uses the same atomic boundary with `build_p700_production.py`. Its
separate wing and tail meshes contain authored `STOWED` and `DEPLOYED`
transforms. `validate_antey_crossfit.py` appends that actual saved P700 asset
and instances it at all 24 saved Antey hardpoints before export approval.

`export_runtime_glb.py` selects only `runtime_export` mesh nodes. Empty
hardpoints, references, gameplay volumes, collision proxies, cameras, lights,
and review helpers never enter GLB. Animation export is explicit: use
`--animation-contract none` for Antey (its cover motion is authoring-only) and
`--animation-contract P700_Deploy` only for the P700 missile asset. The
exporter does not include every Blender Action implicitly. Future
`P700CoverState_*` interpolation remains owned by LauncherSystem/Simulation.
`validate_runtime_glb.py` imports the GLB into a factory-empty scene and
rejects missing runtime geometry or leaked helpers. Runtime integration
remains deferred until human art and legal gates are closed.
