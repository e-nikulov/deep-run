# M5 V2 — P-700 Granit runtime integration

Status: PRODUCTION RUNTIME INTEGRATED, CI PENDING. Headless lifecycle, canonical production asset staging, 24-slot Antey launcher inventory, Weapon Selector, production launch-anchor materialization and lifecycle-driven LOD0 presentation are implemented. Dedicated >20 km visual acceptance remains open.

## Authority and employment

P-700 launch authorization consumes a perceived `Track` plus own-carrier and production-launcher state. It does not accept a hostile `PhysicsBodyHandle`, authoritative hostile Transform, or entity identity. The existing canonical employment envelope remains unchanged: carrier launch depth `0..50 m`, perceived target range `20..550 km`, carrier speed up to `5 kt`, with the documented gameplay target-sector policy. Surface launch is therefore valid; a submerged launch deeper than 50 m remains rejected.

## Lifecycle

The bounded runtime owns these phases:

`Stored -> UnderwaterLaunch -> WaterExit -> AirborneDeploying -> Cruise -> Terminal -> Impact -> Spent`

A surface launch intentionally bypasses `UnderwaterLaunch` and begins at `WaterExit`. A submerged launch follows the production launcher direction until it physically crosses the authoritative surface level. `deploymentProgress` is exactly zero throughout `UnderwaterLaunch` and `WaterExit`; only `AirborneDeploying` may advance it, and `Cruise` cannot begin before it reaches one. Production presentation consumes this progress to drive the six canonical LOD0 movable surfaces; it does not evaluate the glTF animation as simulation authority.

Guidance retains only the launch `trackId`, last qualified perceived aim point, and perceived position uncertainty. A different Track identity is rejected. Degraded same-ID evidence cannot erase the last qualified solution. Airborne steering, transition timings, speeds, turn rate, terminal threshold and coarse collision/payload values in the current playground are explicit GAME POLICY and are not represented as historical classified flight-control data.

## Production carrier and asset boundary

The canonical `P700_Granit.glb` is staged through the hash-validated production asset path and covered by a dedicated production gate. The gate validates the single `P700_Deploy` animation, 24 unique animation targets (six movable surfaces across four LODs), eight authored LOD0 mesh objects and the exact 25,172-triangle LOD0 total. Renderer primitive/material splits are allowed to produce more draw calls than authored mesh objects.

The accepted Antey production definition supplies exactly 24 opaque `p700LaunchAnchors`. Game-owned launcher inventory tracks only `Loaded -> Spent` state and does not duplicate launcher coordinates or parse a second sidecar. World launch geometry is composed from the accepted model-to-body collision-center correction, the live physics pose and the authoritative 2.5D facing. Launch geometry is rejected while the carrier is inside its smooth turnaround.

Normal play now retains the immutable production carrier launch contract alongside the mutable 24-slot inventory. An accepted P-700 launch materializes from the first loaded production slot, builds the exact world launch anchor from current ownship physics state, launches the Simulation P-700 runtime from that anchor, and only then marks that launcher slot Spent. Failed or disallowed employment cannot consume a launcher.

## Weapon Selector

The normal-play selector contains exactly the two currently implemented player weapons: `USET-80` and `P-700 GRANIT`. Controller parity is D-pad Left/Right; keyboard parity is `Z/C`. The existing `LT/R` Prepare and `RT/Enter` Fire commands apply to whichever profile is selected. Selection is legal only while the current weapon readiness state is `Stored`, so target/readiness state cannot bleed between weapon profiles. P-700 is unavailable when the production carrier/inventory is absent or exhausted.

The combat UI projects the selected weapon and remaining P-700 launcher count without exposing launcher node names, target entity identity or hostile physics handles.

## Physical terminal authority

Movement uses bounded fixed substeps and `PhysicsWorld::SweepBoxClosest`; only a backend-confirmed physical sweep hit can create `Impact`, expose the impacted body, emit coarse damage/explosion events, and then consume the weapon into `Spent`. No distance-only duplicate collision authority exists.

The regression fixture uses a target beyond the real 20 km minimum range rather than weakening the employment contract to fit the existing 1.8 km torpedo/destroyer playground. It reuses the same initialized `PhysicsWorld` as the surrounding M5 combat-impact checks, matching the production single-world composition instead of attempting to initialize a second Jolt backend. It covers surface and submerged launch, depth/speed/minimum-range rejection, stowed water exit, post-exit deployment, Cruise/Terminal transition, Track-identity protection, physical impact/damage/explosion, and one-way consumption into `Spent`.

## Remaining acceptance slice

The existing 1.8 km destroyer fixture remains intentionally torpedo-scale and therefore cannot legally authorize P-700 employment. Do not reduce the canonical 20 km minimum. The remaining acceptance work is a dedicated >=20 km P-700 scenario that visibly separates production-anchor launch, water exit, post-exit deployment, cruise/terminal presentation and physical impact/explosion while preserving the same perception and physics authority boundaries.
