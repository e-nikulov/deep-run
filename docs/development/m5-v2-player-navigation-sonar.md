# M5-V2 — Player Navigation / Dive Controls / Sonar Visualization Closure

Status: IN PROGRESS

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

M5 itself remains OPEN because manual review exposed player-comprehension/runtime gaps outside camera presentation.

## Goal

Close the remaining normal-player usability gaps before Milestone 5 can be declared COMPLETE.

M5-V2 is limited to player navigation and presentation of already-existing vessel/acoustic simulation. It must not start P-700 runtime, M6 compartments/flooding, crew simulation, power management, or new combat AI.

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

- P-700 launch/runtime;
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

Only after human acceptance of these items may M5 be marked COMPLETE. P-700 runtime begins afterward as its own integration scope.
