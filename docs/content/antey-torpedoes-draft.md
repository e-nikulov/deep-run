# Antey torpedoes — visual review draft

Status: **VISUAL_REVIEW_REQUIRED**.

This branch introduces content-only review drafts for the two conventional torpedo classes planned for the Project 949A-inspired Antey package. It does **not** change M5 gameplay, weapon simulation, seeker logic, damage, collision authority, acoustic authority, or Antey runtime staging.

## Review set

| Asset | Intended tube | Nominal body dimensions | Review geometry |
|---|---:|---:|---:|
| USET-80-inspired draft | 533 mm | 7.900 m x 0.533 m | 257 vertices / 404 triangles |
| 65-76A Kit-inspired draft | 650 mm | 11.300 m x 0.650 m | 257 vertices / 404 triangles |

The GLBs use `+X` forward and metres. The quoted diameter is the nominal cylindrical body diameter; the tail-control / propulsor envelope is intentionally wider.

## Provenance boundary

The uploaded low-poly torpedo pack was useful for selecting the desired low-poly visual density and for choosing a rough donor-like silhouette direction. To avoid importing unknown third-party geometry into the public repository, the committed meshes are **original procedural geometry**. No donor vertices, UVs, textures, materials, mesh topology, or embedded asset data were copied.

## What is ready for review

The current draft establishes only the large visual decisions: overall length/diameter ratio, rounded nose, cylindrical body, tapered afterbody, cruciform tail surfaces, and a simplified twin-propulsor treatment. It is deliberately cheap enough to iterate before production authoring.

## Visual gate

User review should answer:

- is the overall USET-80 silhouette convincing at Deep Run camera distances;
- is the 65-76A visibly heavier/longer than the 533 mm weapon;
- should the nose profile be blunter or more elongated;
- should the tail surfaces be smaller/larger;
- should the simplified propulsor be kept, redesigned, or hidden by a tail shroud.

No asset in this directory should be promoted to `Engine/Assets`, bound to Antey launch anchors, or treated as accepted gameplay content until this visual gate passes.

## After approval

After visual acceptance, convert the chosen geometry into the normal source-first production path: authoring BLEND, production material/normals, validation, runtime GLB, sidecar metadata, fit checks against the existing 4x533 + 2x650 Antey tube contract, and only then runtime presentation integration.

The review GLBs can be regenerated deterministically with:

```powershell
python Tools/Blender/generate_antey_torpedo_review_assets.py
```
