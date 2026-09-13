# Antey propeller runtime presentation repair

Status: validation pending on branch `fix/antey-propeller-runtime-presentation`.

## Observed regression

The canonical `Content/submarines/Antey/Antey.glb` contains the accepted twin seven-blade production propellers, but the normal side-view runtime could make them read as oversized flat plates and could drop blade faces as the shafts rotated. The asset itself is not modified by this repair.

## Repair contract

- Propeller drawable subtrees remain resolved from the opaque `SM_Propeller_Port` / `SM_Propeller_Starboard` semantic roots.
- Runtime shaft rotation is re-based around the geometry-derived production `localOrigin` (hub centre) from `Antey.authoring.json`, instead of assuming the imported transform-only root origin is the mechanical pivot.
- The explicit pivot path is presentation-only and must preserve every vertex distance from the hub centre under shaft rotation; no scale, shear, orbit, or translation of the propeller assembly is allowed.
- Normal production framing uses an 8-degree presentation-only side yaw. World Y remains screen-vertical, so the accepted underwater surface/seabed vertical composition is unchanged, while the small depth cant makes the real twin-propeller geometry readable.
- The glTF `material.doubleSided` flag is now preserved by `GltfModelLoader`. D3D12 keeps the normal back-face-culling PSO for one-sided materials and switches only authored double-sided draws to a no-cull model PSO. This restores the reverse-facing portions of the thin propeller blades instead of duplicating geometry or disabling culling globally.
- The production Antey regression requires `MAT_Antey_Propellers` to import as double-sided and verifies that every submitted propeller draw retains that material contract.
- Physics, collision, buoyancy, propulsion authority, shaft RPM, acoustic authority, weapons, and the canonical `Antey.glb` remain unchanged.

## Acceptance

Acceptance requires Debug and Release build/CTest plus the existing windowed combat and P-700 smokes. The headless production-asset regression additionally rotates each production propeller by 90 degrees around its explicit hub pivot and verifies that the squared distance of every submitted propeller vertex from that pivot is invariant within tolerance. It also fails if the canonical propeller material loses its glTF double-sided contract.
