# Antey torpedoes — visual review

Status: **V2 VISUAL_REVIEW_REQUIRED**.

This branch contains content-only visual-review candidates for the two conventional torpedo classes planned for the Project 949A-inspired Antey package. It does **not** change M5 gameplay, weapon simulation, seeker logic, damage, collision authority, acoustic authority, launch logic, or Antey runtime staging.

## Review set

| Asset | Intended tube | Nominal body dimensions | V2 geometry |
|---|---:|---:|---:|
| USET-80-inspired candidate | 533 mm | 7.900 m x 0.533 m | 389 vertices / 656 triangles |
| 65-76A Kit-inspired candidate | 650 mm | 11.300 m x 0.650 m | 433 vertices / 736 triangles |

The V2 review assets are committed as binary glTF 2.0 (`*.glb`) and use `+X` forward and metres. The nominal length is the complete visual envelope including the aft propeller pair. The quoted diameter is the cylindrical body calibre; control surfaces and propellers are intentionally wider.

## V1 review result

V1 passed as a blockout but did not pass final visual review. The main problems were that both weapons read as the same generic silhouette at two scales, the tail/root surfaces were too long, the propulsion group was too schematic, and the nose treatment was too neutral.

## V2 changes

V2 keeps the accepted dimensions but deliberately separates the silhouettes:

- **USET-80-inspired**: longer ogive nose, compact 533 mm body impression, slimmer afterbody, smaller compact cruciform tail.
- **65-76A-inspired**: blunter nose, visibly larger 650 mm body, stockier afterbody, larger/heavier tail surfaces.
- Tail planes are now compact and concentrated at the afterbody rather than extending far forward.
- Each weapon has two separate coaxial propeller mesh/nodes.
- The propellers have intentionally different blade counts and opposite blade handedness to improve readability.

## Propeller presentation contract

The two coaxial propellers are separate nodes with pivots on the torpedo longitudinal axis. The V2 review GLBs include `CounterRotatingPropellers_Review`:

- `Propeller_A`: rotation around `+X` in the positive direction;
- `Propeller_B`: rotation around `+X` in the negative direction;
- both rotate at equal review angular speed;
- the pair therefore counter-rotates.

This animation is **presentation-owned only** at this stage. It does not create propulsion authority, RPM simulation, hydrodynamic torque, gameplay state, or a weapon-system dependency. Runtime/gameplay ownership remains deferred until the torpedo content is visually accepted and promoted through the normal production path.

## Provenance boundary

The uploaded low-poly torpedo pack is reference-only. The committed meshes are original procedural geometry. No donor vertices, UVs, textures, materials, topology, animations, or embedded asset data are copied into the repository.

## V2 visual gate

User review should answer:

- does the USET-80-inspired candidate now read as the compact/standard 533 mm weapon;
- does the 65-76A-inspired candidate read as the larger/heavier 650 mm weapon rather than a scaled copy;
- are both nose profiles acceptable;
- are the new compact tail surfaces acceptable;
- is the coaxial counter-rotating propeller treatment visually acceptable;
- is either tail/propeller group too large at normal Deep Run camera distances.

No asset in this directory should be promoted to `Engine/Assets`, bound to Antey launch anchors, or treated as accepted gameplay content until this V2 visual gate passes.

## After approval

After visual acceptance, convert the chosen geometry into the normal source-first production path: authoring BLEND, production materials/normals, LODs, validation, runtime GLB, sidecar metadata, fit checks against the existing `4x533 + 2x650` Antey tube contract, and only then runtime presentation integration.

V2 remains a review artifact rather than an accepted production source. After user visual PASS, the selected geometry will be consolidated into the normal source-first authoring/validation pipeline; the earlier V1 blockout generator is retained only as iteration lineage and is not authoritative production geometry.
