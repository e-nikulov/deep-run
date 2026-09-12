# M5 V2 — P-700 Granit runtime integration

Status: CANDIDATE. Headless lifecycle contract implemented; production visual asset/launcher integration and Weapon Selector remain next.

## Authority and employment

P-700 launch authorization consumes a perceived `Track` plus own-carrier and production-launcher state. It does not accept a hostile `PhysicsBodyHandle`, authoritative hostile Transform, or entity identity. The existing canonical employment envelope remains unchanged: carrier launch depth `0..50 m`, perceived target range `20..550 km`, carrier speed up to `5 kt`, with the documented gameplay target-sector policy. Surface launch is therefore valid; a submerged launch deeper than 50 m remains rejected.

## Lifecycle

The bounded runtime owns these phases:

`Stored -> UnderwaterLaunch -> WaterExit -> AirborneDeploying -> Cruise -> Terminal -> Impact -> Spent`

A surface launch intentionally bypasses `UnderwaterLaunch` and begins at `WaterExit`. A submerged launch follows the production launcher direction until it physically crosses the authoritative surface level. `deploymentProgress` is exactly zero throughout `UnderwaterLaunch` and `WaterExit`; only `AirborneDeploying` may advance it, and `Cruise` cannot begin before it reaches one. This is the runtime gate that will drive the authored `P700_Deploy` presentation after water exit rather than inside the launcher/underwater path.

Guidance retains only the launch `trackId`, last qualified perceived aim point, and perceived position uncertainty. A different Track identity is rejected. Degraded same-ID evidence cannot erase the last qualified solution. Airborne steering, transition timings, speeds, turn rate, terminal threshold and coarse collision/payload values in the current playground are explicit GAME POLICY and are not represented as historical classified flight-control data.

## Physical terminal authority

Movement uses bounded fixed substeps and `PhysicsWorld::SweepBoxClosest`; only a backend-confirmed physical sweep hit can create `Impact`, expose the impacted body, emit coarse damage/explosion events, and then consume the weapon into `Spent`. No distance-only duplicate collision authority exists.

The regression fixture uses a target beyond the real 20 km minimum range rather than weakening the employment contract to fit the existing 1.8 km torpedo/destroyer playground. It reuses the same initialized `PhysicsWorld` as the surrounding M5 combat-impact checks, matching the production single-world composition instead of attempting to initialize a second Jolt backend. It covers surface and submerged launch, depth/speed/minimum-range rejection, stowed water exit, post-exit deployment, Cruise/Terminal transition, Track-identity protection, physical impact/damage/explosion, and one-way consumption into `Spent`.

## Next integration slice

1. Load and stage the canonical `P700_Granit.glb` without weakening the engine's animation/skin validation boundary.
2. Bind the 24 production Antey `p700LaunchAnchors` and hatch groups to the runtime.
3. Drive the six authored movable surfaces from the canonical `P700_Deploy` contract only during `AirborneDeploying`.
4. Add one controller-first Weapon Selector with keyboard parity; do not introduce a separate P-700-only hotkey scheme.
5. Add the dedicated >20 km P-700 visual playground and final manual/CI acceptance.
