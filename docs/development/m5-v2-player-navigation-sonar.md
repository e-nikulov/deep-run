# M5-V2 — Player Navigation / Dive Controls / Sonar Visualization Closure

Status: ACCEPTED / COMPLETE — final Debug/Release gate: CI `34712161730` (#488) on `851a922dd4a2f4055dba523d3dc1a7773dad4ed2`.

Base branch: `feature/m5-v2-player-navigation-sonar`

Base accepted camera SHA: `54487271becdb3572ab62a23b246291a30005c92`

## Accepted M5 presentation baseline

The M5 camera/presentation repair is accepted by human review through STOP 2.

Accepted behavior:

- normal/local camera scale and underwater-first composition;
- continuous surface framing with no 600 m presentation snap;
- tactical/operational/strategic zoom transitions;
- day sky baseline and tactical sky priority;
- bathymetry/deep-ocean presentation rules;
- scale HUD and ownship >= 1 projected-pixel zoom cap;
- operational/strategic symbol presentation.

These accepted presentation behaviors must not regress during M5-V2.

Those player-comprehension/runtime gaps are now closed for M5: production ownship follow, dive telemetry/control-surface presentation and perceived-world sonar visualization are integrated without moving simulation authority into presentation.

## Goal

Close the remaining normal-player usability gaps required for the final Milestone 5 acceptance.

M5-V2 itself remains limited to player navigation and presentation of already-existing vessel/acoustic simulation. P-700 was subsequently integrated as a separate final M5 scope; M6 compartments/flooding, crew simulation, power management and new combat AI remain outside V2.

## V2-A — Ownship navigation / camera follow

Problem: the production submarine physically receives throttle, but the historical M2/M3 playground camera remains anchored to the initial scene center. The player therefore experiences the local playground as if forward travel ends near the old 600–800 m authored section.

Required behavior:

- normal/local camera follows the authoritative production Antey body position;
- camera movement never writes simulation state;
- zoom remains independent from ownship translation;
- accepted tactical/operational/strategic camera behavior remains available;
- moving beyond the original local authored section must not fabricate repeated physical terrain;
- transition from local authored environment to deep-water/wide presentation must be explicit and coherent.

A full streamed/procedural world is NOT part of M5-V2. Future world traversal must use shared deterministic bathymetry authority rather than tiled fake collision.

## V2-B — Dive-control presentation

Existing simulation already accepts semantic `Depth` input and evaluates bow/stern control-surface forces.

Required presentation closure:

- production bow/stern plane visuals reflect committed simulation control-surface deflection;
- visuals must read committed state, not raw keyboard/controller input;
- expose player-readable depth information in HUD: signed depth, vertical speed, throttle and control-plane state;
- preserve authoritative Jolt pitch/position as the vessel pose source;
- no depth-hold/autopilot is required for M5 unless later explicitly scoped.

## V2-C — Active-sonar educational visualization

The active sonar simulation and round-trip perception path are already authoritative. The missing piece is presentation.

One accepted player ping should produce one restrained visualization:

1. one forward pulse/wavefront emitted from the production bow-array semantic;
2. pulse fades with propagation distance/time;
3. on a real simulated reflection, one reflected echo wavefront originates at the reflected contact position;
4. reflected wave travels back toward ownship;
5. no additional timing, detection, ranging or Track state may be invented by presentation.

The presentation should help the player understand active sonar without filling the whole screen with repeated rings.

## Sonar visualization option

Add a user-facing presentation option:

`Sonar visualization: On / Off`

Default: `On`.

Turning it off affects visuals only. It must not alter:

- acoustic simulation;
- propagation delay;
- active pulse/echo timing;
- SensorObservation;
- TrackManager;
- weapon targeting or guidance.

## Explicit non-goals

M5-V2 does not implement:

- P-700 launch/runtime inside the V2 slice (it was subsequently closed as separate final M5 integration);
- final procedural world generation;
- full ocean streaming;
- M6 compartment/flooding/system damage;
- crew simulation;
- final settings UI framework;
- final sonar art/VFX quality.

## Closure gate

M5-V2 requires:

- Debug and Release configure/build/CTest PASS;
- windowed smoke PASS;
- accepted camera presentation remains regression-free;
- manual ownship travel beyond the original local camera footprint;
- manual dive/surface test with visible production-plane deflection and depth HUD feedback;
- manual active sonar ping showing outgoing pulse and reflected echo with real acoustic timing;
- sonar visualization option demonstrably disables visuals without altering ranging behavior.

Closure result: ACCEPTED. The retained camera/presentation baseline remains intact, the navigation/dive/sonar state is covered by the M5 regression suite, `Sonar visualization: On / Off` defaults to `On` and changes presentation only, and the final clean Debug/Release CI gate passed in run `34712161730` (#488). P-700 was then closed as a separate final M5 integration without changing these V2 authority boundaries.

## Main asset sync / torpedo playground binding

The V2 branch is merged with current `main` Antey content before further navigation/sonar work. The current
Project 949A compartment contract and staged production Antey GLB are authoritative on this branch.

Windowed combat playground presentation also loads the current-main V2 torpedo review GLBs directly from
`Content/Weapons/Torpedoes`: USET-80-inspired for the player torpedo and 65-76A-inspired for the hostile test
torpedo. This pairing exists only to exercise both visual candidates in the playground. It does not change
weapon simulation, tube/loadout truth, collision, guidance, seeker, damage, acoustic authority, or production
promotion status. Both GLBs render at authored metre scale; the old 24 x 5 x 5 proxy-cube torpedo visual is not
used when a live underwater torpedo is rendered.

The merged Antey GLB node contract was inspected directly from its glTF JSON chunk after the sync. V2 depth-plane
bindings use `SM_Antey_LOD0_BowPlane_Port`, `SM_Antey_LOD0_BowPlane_Starboard`,
`SM_Antey_LOD0_SternPlane_Port`, and `SM_Antey_LOD0_SternPlane_Starboard`. This keeps the V2 articulation layer
aligned with the current-main production GLB rather than stale pre-merge `TailPlane` authoring names.

The 2026-09-12 resync takes `main` commit `89991d702502fbea0bfa8024520a4c46aa3136dc` as the authoritative Antey
production content again. The accepted Ventral Rudder repair, GameReady BLEND, runtime GLB, mirrored Engine
runtime package and ventral authoring/validation tooling are preserved byte-for-byte from that `main` state.
M5-V2 adapts runtime/tests to these assets rather than rewriting the model. The historical IG1-C source gate now
checks the production model draw boundary without depending on the obsolete name of the node-override container;
the actual prohibition on prototype physics/model paths remains unchanged.

Current `main` no longer duplicates V2-only `controlSurfaces` records in `Antey.authoring.json`. The M5-V2 loader
therefore consumes accepted `controlSurfaceAuthoring` identity from `Antey.asset.json` when those explicit records
are absent, resolves the corresponding four production GLB nodes privately, and exposes only semantic bow/stern
groups plus opaque presentation binding indices to runtime code. This compatibility boundary does not modify the
accepted model, GLB, physics authority, or simulation ownership of control-surface angle.
