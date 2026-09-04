# P700 Production Asset Contract

CONTENT STATUS: TECHNICALLY VALIDATED; HUMAN ART APPROVAL PENDING

LICENSE STATUS: SHIPPING BLOCKED PENDING LEGAL REVIEW

The canonical provenance record classifies the source and derivative production
asset as `NON-SHIPPABLE UNTIL RESOLVED`; see
[`asset-provenance.md`](asset-provenance.md) and
[`third-party/p700-asset-license-review.md`](third-party/p700-asset-license-review.md).
This legal gate is independent of technical validation and human-art approval.

## Canonical files

| Purpose | Path |
|---|---|
| Immutable artistic source | `Content/Weapons/P700/P700_Granit.blend` |
| Editable production asset | `Content/Weapons/P700/P700_Granit_GameReady.blend` |
| Runtime render asset | `Content/Weapons/P700/P700_Granit.glb` |
| Render metadata | `Content/Weapons/P700/P700.asset.json` |
| State/pivot authoring | `Content/Weapons/P700/P700.authoring.json` |
| Reference provenance | `Content/Weapons/P700/References/reference_manifest.md` |

The production hierarchy has separate body, booster, two main wings, and four
tail surfaces. Every movable surface has independent mesh data and authored
transforms at frame 1 (`STOWED`) and frame 41 (`DEPLOYED`). No hide/show swap is
used: the actual geometry moves.

Every movable object origin is on its physical longitudinal root hinge. The
origin remains fixed for the complete operation; animation channels contain
rotation only. Five canonical review samples use frames 1, 11, 21, 31, and 41
for 0%, 25%, 50%, 75%, and 100% deployment. The six Blender source actions are
normalized through the shared `P700_Deploy` NLA track into one GLB animation
named `P700_Deploy`.

| Packing measure | Value |
|---|---:|
| Body diameter | 0.884 m |
| Maximum stowed diameter | 1.262 m |
| Launcher envelope diameter | 1.350 m |
| Radial clearance | 0.0438 m |
| `minimumRequiredClearance` | 0.0250 m |

The stowed main wings and tail surfaces lie compactly around the body. The
fixed-root, rotation-only path has no movable-surface self-intersection at any
of the five validation samples. The missile remains `STOWED` through launcher
exit and underwater launch. A future `WeaponSystem` may invoke
`P700_Deploy` only in the appropriate post-launch phase.

| LOD | Objects | Vertices | Triangles |
|---|---:|---:|---:|
| LOD0 | 8 | 12,632 | 25,172 |
| LOD1 | 8 | 7,113 | 14,134 |
| LOD2 | 8 | 3,339 | 6,586 |
| LOD3 | 8 | 1,170 | 2,248 |

The production meshes retain UV maps and use two metallic-roughness-ready
materials. The GLB contains all four LODs and one deterministic deployment
animation, but no reference meshes, authoring helpers, cameras, or lights.
Future weapon simulation owns deployment state, timing, propulsion, guidance,
and effects.
