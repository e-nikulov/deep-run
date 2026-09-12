# M5 V2 — P-700 Granit runtime integration

Status: INTEGRATION IN PROGRESS. Headless lifecycle contract, canonical production asset staging, 24-slot Antey launcher inventory and production launch-anchor composition are implemented. Weapon Selector/runtime materialization and dedicated >20 km visual acceptance remain open.

## Authority and employment

P-700 launch authorization consumes a perceived `Track` plus own-carrier and production-launcher state. It does not accept a hostile `PhysicsBodyHandle`, authoritative hostile Transform, or entity identity. The existing canonical employment envelope remains unchanged: carrier launch depth `0..50 m`, perceived target range `20..550 km`, carrier speed up to `5 kt`, with the documented gameplay target-sector policy. Surface launch is therefore valid; a submerged launch deeper than 50 m remains rejected.

## Lifecycle

The bounded runtime owns these phases:

`Stored -> UnderwaterLaunch -> WaterExit -> AirborneDeploying -> Cruise -> Terminal -> Impact -> Spent`

A surface launch intentionally bypasses `UnderwaterLaunch` and begins at `WaterExit`. A submerged launch follows the production launcher direction until it physically crosses the authoritative surface level. `deploymentProgress` is exactly zero throughout `UnderwaterLaunch` and `WaterExit`; only `AirborneDeploying` may advance it, and `Cruise` cannot begin before it reaches one. This is the runtime gate that will drive the authored `P700_Deploy` presentation after water exit rather than inside the launcher/underwater path.

Guidance retains only the launch `trackId`, last qualified perceived aim point, and perceived position uncertainty. A different Track identity is rejected. Degraded same-ID evidence cannot erase the last qualified solution. Airborne steering, transition timings, speeds, turn rate, terminal threshold and coarse collision/payload values in the current playground are explicit GAME POLICY and are not represented as historical classified flight-control data.

## Production carrier and asset boundary

The canonical `P700_Granit.glb` is staged through the hash-validated production asset path and covered by a dedicated production gate. The gate validates the single `P700_Deploy` animation, 24 unique animation targets (six movable surfaces across four LODs), eight authored LOD0 mesh objects and the exact 25,172-triangle LOD0 total. Renderer primitive/material splits are allowed to produce more draw calls than authored mesh objects.

The accepted Antey production definition supplies exactly 24 opaque `p700LaunchAnchors`. Game-owned launcher inventory tracks only `Loaded -> Spent` state and does not duplicate launcher coordinates or parse a second sidecar. World launch geometry is composed from the accepted model-to-body collision-center correction, the live physics pose and the authoritative 2.5D facing. Launch geometry is rejected while the carrier is inside its smooth turnaround. The launcher-inventory regression links the shared `PhysicsRenderSync` transform implementation so test and production use the same quaternion/point transform path.

## Physical terminal authority

Movement uses bounded fixed substeps and `PhysicsWorld::SweepBoxClosest`; only a backend-confirmed physical sweep hit can create `Impact`, expose the impacted body, emit coarse damage/explosion events, and then consume the weapon into `Spent`. No distance-only duplicate collision authority exists.

The regression fixture uses a target beyond the real 20 km minimum range rather than weakening the employment contract to fit the existing 1.8 km torpedo/destroyer playground. It reuses the same initialized `PhysicsWorld` as the surrounding M5 combat-impact checks, matching the production single-world composition instead of attempting to initialize a second Jolt backend. It covers surface and submerged launch, depth/speed/minimum-range rejection, stowed water exit, post-exit deployment, Cruise/Terminal transition, Track-identity protection, physical impact/damage/explosion, and one-way consumption into `Spent`.

## Next integration slice

1. Complete the controller-first Weapon Selector with keyboard parity and make the selected profile own Prepare/Fire qualification; do not introduce a separate P-700-only hotkey scheme.
2. Materialize P-700 from one loaded production launcher slot using the perceived selected Track and current carrier state.
3. Drive the six authored movable surfaces from the canonical `P700_Deploy` contract only during `AirborneDeploying`.
4. Add the dedicated >20 km P-700 visual playground and final manual/CI acceptance.
