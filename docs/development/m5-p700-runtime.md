# M5 V2 — P-700 Granit runtime integration

Status: FULL LAUNCH / FLIGHT / TERMINAL SEQUENCE IMPLEMENTED, CI VALIDATION ACTIVE. Headless lifecycle, canonical production asset staging, 24-slot Antey launcher inventory, Weapon Selector, production launch-anchor materialization, paired production hatch presentation, lifecycle-driven LOD0 presentation and the dedicated >20 km production acceptance scenario are implemented.

## Authority and employment

P-700 launch authorization consumes a perceived `Track` plus own-carrier and production-launcher state. It does not accept a hostile `PhysicsBodyHandle`, authoritative hostile Transform, or entity identity. The canonical gameplay employment envelope remains unchanged: carrier launch depth `0..50 m`, perceived target range `20..550 km`, carrier speed up to `5 kt`, with the documented gameplay target-sector policy. Surface launch is valid; a submerged launch deeper than 50 m is rejected.

## Lifecycle

The bounded runtime owns these phases:

`Stored -> HatchOpening -> UnderwaterLaunch -> WaterExit -> PostExitTransition -> AirborneDeploying -> Cruise -> Terminal -> (Impact | Defeated) -> Spent`

Fire commits the selected launcher, but the missile does not move until its paired production hatch reaches fully open. A submerged launch then uses the attached launch booster until physical water crossing. `WaterExit` remains folded and booster-driven. `PostExitTransition` removes the protective nose fairing, separates the booster and transfers thrust authority to the main engine. Only after that may `AirborneDeploying` advance `deploymentProgress`; `Cruise` is impossible until deployment reaches one.

Guidance retains only the launch `trackId`, last qualified perceived aim point, and perceived position uncertainty. A different Track identity is rejected. Degraded same-ID evidence cannot erase the last qualified solution. Airborne steering, transition timings, speeds, turn rate, terminal threshold and coarse collision/payload values are explicit GAME POLICY and are not represented as historical/classified flight-control data.

## Production carrier and asset boundary

The canonical `P700_Granit.glb` remains hash-validated through the production asset path. The production gate validates the single `P700_Deploy` animation, 24 unique animation targets (six movable surfaces across four LODs), eight authored LOD0 mesh objects and the exact 25,172-triangle LOD0 total.

The accepted Antey production definition supplies exactly 24 opaque P-700 launch anchors in 12 paired hatch groups. Each group resolves once at the asset boundary to one of the twelve actual production hatch meshes; gameplay retains only the semantic hatch-group identity and opaque renderer binding. A launcher is marked `Spent` only after the launch solution and P-700 runtime materialization succeed.

The production P-700 booster is a distinct canonical mesh. Once separation occurs it is removed from the missile draw set and briefly rendered as detached presentation geometry. The current canonical source asset does not contain a separately authored protective nose cap, so that cap is deliberately a presentation-only fairing proxy and never participates in collision, bounds, targeting or damage. The current Antey art also does not publish exact P-700 hatch hinge pivots; the hatch therefore uses a bounded lift-open presentation rather than inventing an undocumented hinge axis.

## Weapon Selector

Normal play exposes exactly the two implemented player weapons: `USET-80` and `P-700 GRANIT`. Controller parity is D-pad Left/Right; keyboard parity is `Z/C`. Existing `LT/R` Prepare and `RT/Enter` Fire commands apply to the selected profile. Selection is legal only while readiness is `Stored`, preventing readiness/target state from bleeding between weapon profiles. P-700 is unavailable when the production carrier/inventory is absent or exhausted.

## Flight / payload gameplay policy

Current explicit GAME POLICY values are:

- underwater booster exit: `50 m/s`
- water exit: `100 m/s`
- post-exit/deployment flight: `180 m/s`
- cruise: `680 m/s`
- terminal: `750 m/s`
- employment range: `20..550 km`
- direct impact damage: `100 HP`
- coarse explosion radius: `30 m`

These values are gameplay tuning, not historical/classified exact P-700 performance claims.

## Terminal effectiveness and countermeasures

Normal-play terminal effectiveness is deterministic-seeded per launch so replays/CI are reproducible while separate launches can resolve differently. The current GAME POLICY applies independent opportunities for:

- base seeker failure: `4%`, plus up to `20` percentage points from perceived position uncertainty;
- soft-kill / deception: `10%`;
- hard-kill interception: `16%`;
- maneuver defeat: `4%`.

A failed terminal outcome enters `Defeated` and cannot fabricate collision damage. A clean terminal solution still requires `PhysicsWorld::SweepBoxClosest` to produce a real physical hit before `Impact`, damage and explosion are emitted.

## Acceptance

The existing 1.8 km destroyer fixture remains torpedo-scale and is not weakened to fit P-700. A dedicated `--smoke-p700` production scenario starts the Antey at 30 m and places the physical surface target at 20.1 km. Its commander issues only the same semantic commands available to normal input: select P-700, select perceived Track, active range it, prepare and fire. The acceptance path then requires observable lifecycle evidence for hatch opening, underwater booster exit, water exit, hardware separation, aerodynamic deployment, cruise, terminal flight and physical impact. Defensive defeat is disabled only in this dedicated impact-proof scenario; the headless regression separately forces a 100% hard-kill profile and proves `Defeated -> Spent` without a fake hit.

The production package retains its legal status: `SHIPPING BLOCKED PENDING LEGAL REVIEW`.
