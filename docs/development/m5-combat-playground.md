# Milestone 5 — Combat Playground

Status: IN PROGRESS

## Current accepted baseline

The stable M5 branch now includes the A-D weapon/perception/impact foundation; live decoy seeker integration;
simple destroyer AI with active fire-control ranging; production-bound live naval mine collision; J1/J2
controller-first commander commands; H/H.4 combat/environment presentation; and F.2 reciprocal destroyer
torpedo threat. F.2 was stabilized by extending only the deterministic headless engagement horizon from
45 s to 80 s so the legitimately delayed F.1 ranged fire-control launch has enough SimulationTime to traverse
the ~1.7 km scenario. Gameplay speed/guidance/damage were unchanged. Stable evidence: clean candidate SHA
`3fd4cad9faf9d89787348523dfcb4e20ed1aaac9` passed Debug and Release twice, and feature commit
`1d90159ac236cc3cc2df0a1d07b6fbf4c4d5fdc1` passed post-merge CI run `34520094788` in both configurations.
J3 is additionally accepted at feature commit `3a6fef335878046808e9caa08d3b4df8ceb1bcaf`; post-merge CI
run `34523462484` passed Debug and Release configure, build, CTest, windowed smoke and visual artifacts.

### M5-J3 — perceived incoming-threat combat UI

Status: ACCEPTED.

J3 does not expose hostile torpedo position, range, Transform or PhysicsBodyHandle to commander UI. A dedicated
passive-acoustic perceived-world path samples the simulated hostile weapon only as timestamped AcousticEmission
values, preserves propagation delay before they can reach the production player passive receiver through
AcousticWorld, converts observations through SensorObservation and TrackManager, and projects only lifecycle,
bearing, bearing uncertainty and confidence. The warning naturally
coasts/clears after the physical threat is consumed. No tactical pause or generic command queue is introduced.

### M5-J4 — player defensive acoustic countermeasure

Status: ACCEPTED after the normal Debug/Release candidate gate used for promotion to the stable M5 branch.

J4 wires the already-canonical `DeployDecoy` semantic action into normal combat play (`B` on the reference
controller and `F` on keyboard). The command is a one-shot M5 playground resource only; it does not introduce
M6 inventory, compartment, crew or launcher-system simulation. Deployment creates an ordinary moving
`AcousticEmitter` through the existing `AcousticDecoy` contract and projects availability/active state plus
accepted/rejected command feedback to the combat UI.

The reciprocal F.2 torpedo now owns a local passive seeker with its own TrackManager. It samples timestamped
emissions from Antey and the player decoy through `AcousticWorld`, waits for acoustic propagation, converts only
observations into perceived tracks, and chooses a bearing-only cue through the same `TorpedoSeeker` policy used
by the player's weapon. The decoy therefore diverts the torpedo by winning perceived-track selection; no decoy
flag, player body handle, authoritative target Transform, or source identity crosses into seeker guidance.
The accepted automated smoke path never deploys the player decoy, preserving the deterministic F.2 baseline.

## M5-H.1-B — automated windowed visual acceptance

Status: IMPLEMENTED — local Debug/Release verification passed; CI run ID pending the workflow execution for
the resulting commit.

The existing Win32 `WindowFrameCapture` path is now also used by a bounded M5 acceptance harness. The
harness observes authoritative `SimulationTime`, PhysicsWorld body snapshots, the real Jolt-backed torpedo
impact event, and the existing combat presentation draw statistics. It does not perform a second hit or
distance-based collision detector and it does not score subjective image quality.

Required deterministic checkpoints are:

- `M5_COMBAT_INITIAL` — production Antey and surface destroyer are present before launch.
- `M5_TORPEDO_IN_FLIGHT` — the active torpedo is separated from Antey and destroyer.
- `M5_PRE_IMPACT` — the active torpedo is close to the destroyer without an emitted impact.
- `M5_POST_IMPACT` — the Jolt hit targets the destroyer body, the torpedo is spent, integrity is reduced,
  and the explosion uses the physical hit position.
- `M5_RESIZED` — the existing 1280x720 to 1024x640 smoke resize preserves the fixed 600 m side-view,
  finite camera matrices, and the combat GPU presentation handle.

Each checkpoint writes `m5-combat-acceptance.json` state alongside a best-effort BMP capture named
`m5-combat-initial.bmp`, `m5-torpedo-flight.bmp`, `m5-pre-impact.bmp`, `m5-post-impact.bmp`, or
`m5-resized.bmp`. The machine gate validates finite transforms, the destroyer surface band, bounded and
forward torpedo movement, decoy separation, physical impact-body identity, explosion/impact position
agreement, spent-torpedo immobility, the M5 draw contract, camera/aspect stability, GPU handle validity,
and the historical PhysicalPlayground `74/72/364380` regression counters. Image-diff gating is deferred:
the current Win32 compositor capture is not sufficiently driver-deterministic for a useful mandatory pixel
threshold, while the numerical/state gate is deterministic and mandatory.

Automation does not replace human review of apparent destroyer scale relative to Antey, natural torpedo
motion, decoy visual distinction, whether impact/explosion reads on the destroyer, or scene coherence after
resize.

CI run ID: pending (this worktree has no CI run for its uncommitted changes).
Accepted commit SHA: `41ed6422fa79aa56c48b38476af7bedda6c11774` base; update this line with the resulting
commit SHA and CI run ID when the workflow accepts the change.

Milestone 5 begins from the accepted M4 perceived-world boundary. Combat systems must consume observations/contacts/tracks appropriate to their role and must not gain authoritative hostile entity state merely because Simulation knows it.

## M5-A — weapon readiness and track-constrained targeting

Status: ACCEPTED.

The first bounded slice establishes an authoritative weapon runtime without adding torpedo movement, damage, destroyers, decoys, mines, explosions, or combat AI yet.

Accepted contract:

- `WeaponDefinition` owns authored preparation time and minimum targeting quality.
- `WeaponRuntimeState` owns the authoritative phase and advances on monotonic `SimulationTime`.
- Initial phases are deliberately bounded to `Stored -> Preparing -> Ready -> Launched` for this slice.
- target assignment is also a `SimulationTime`-ordered authoritative state change.
- target assignment consumes `Perception::Track` only.
- runtime target state stores only `trackId`; no target entity handle, `Transform`, or other hostile ground-truth identity is accepted or retained.
- the first conventional-heavyweight targeting profile can require an estimated track position, minimum confidence, bounded bearing uncertainty, and an explicitly accepted lifecycle.
- a normal M4 passive bearing-only track therefore remains valid perceived evidence but is insufficient for a position-requiring torpedo launch solution.
- coasting tracks are rejected unless a weapon definition explicitly opts in.
- launch requires both authoritative `Ready` state and an accepted perceived-world track.

`DeepRunM5WeaponRuntimeTests` is a headless CTest target covering definition validation, readiness timing, SimulationTime reversal rejection, bearing-only/weak/uncertain/coasting target rejection, successful qualified-track assignment, launch gating, and the no-ground-truth target-state boundary.

Acceptance evidence: GitHub Actions run `34220540958` on code commit `55fe776a7871611d1210b58d7483eef16f5c856b`. Both `windows-debug` and `windows-release` passed configure, full build, CTest (including `DeepRunM5WeaponRuntimeTests`), and the existing real windowed M4 acoustic smoke gate.

## M5-B — ranged/spatial perceived track

Status: ACCEPTED.

M5-B closes the perception dependency needed by a position-requiring torpedo without granting weapons hostile ground truth.

Accepted contract:

- `SensorObservation` may explicitly carry the observing participant's own sensor position. This is own-platform knowledge, not observed-source truth.
- active acoustic range evidence does not become a spatial target estimate unless own sensor position is also known.
- `TrackManager` derives an estimated 2.5D position only from own sensor position plus perceived bearing/range evidence.
- `Track` carries explicit `positionUncertaintyMeters`; it is not treated as an exact coordinate.
- spatial uncertainty combines range uncertainty with bearing-derived lateral uncertainty and grows over `SimulationTime` from the last ranged estimate.
- later passive bearing-only evidence may update bearing/confidence but does not refresh the age of the last ranged spatial estimate.
- position-requiring weapon definitions now author a maximum accepted spatial uncertainty; missing or excessive uncertainty is rejected by the same perceived-world target-quality gate.
- no hostile entity handle, authoritative source position, or debugger ground truth is added to `SensorObservation`, `Track`, or weapon runtime state.

`DeepRunM5WeaponRuntimeTests` exercises the complete headless path `ActiveSonar echo -> SensorObservation -> TrackManager -> spatial Track -> weapon target-quality validation`. It also proves that identical ranged evidence without own sensor position remains valid evidence but does not fabricate a target position.

Acceptance evidence: GitHub Actions run `34223149715` on code commit `10f6d0451ffb3ef5537f4ba43d33e1ee60676303`. Both `windows-debug` and `windows-release` passed configure, full build, full CTest, and the existing real windowed M4 acoustic smoke gate.

## M5-C — conventional heavyweight torpedo movement/guidance

Status: ACCEPTED.

M5-C introduces the first bounded conventional-heavyweight torpedo runtime without collision or damage yet.

Accepted contract:

- a torpedo runtime can be created only from the matching authoritative `WeaponPhase::Launched` state and accepted perceived `trackId`;
- movement advances only from monotonic `SimulationTime`;
- underwater movement is headless 2.5D kinematics with authored speed and bounded turn rate;
- guidance consumes only the already accepted perceived track identity/spatial estimate and never a hostile entity handle or Transform;
- a later weak/invalid update cannot overwrite the last accepted aim point;
- a different `trackId` cannot silently retarget an in-flight torpedo;
- authoritative weapon and movement timestamps remain synchronized.

The M5 CTest runner executes dedicated conventional-torpedo checks for launch gating, deterministic movement, bounded steering, target identity, stale/weak guidance handling and SimulationTime reversal.

Acceptance evidence: GitHub Actions run `34224207370` on code commit `c7856774c003d66d850c76bca93b48a9f73b3d4b`. Both `windows-debug` and `windows-release` passed configure, full build, full CTest (including the M5-C checks), and the existing real windowed M4 acoustic smoke gate.

## M5-D — swept collision, impact, explosion event and bounded combat damage

Status: ACCEPTED.

M5-D closes the physical-impact boundary without allowing Weapons to reproduce collision logic themselves.

Implemented contract under validation:

- `PhysicsWorld` owns a generic backend-authoritative closest-hit box sweep implemented by the pinned Jolt backend;
- the public sweep API exposes only DeepRun-owned inputs/results (`PhysicsBodyHandle`, hit fraction and swept-box center at contact), never `JPH::*` types;
- callers may explicitly ignore one valid body, allowing launch-platform filtering without weakening other collision authority;
- conventional torpedo motion is first advanced on a candidate copy, then swept from the previous to candidate position; physics query failure cannot partially advance authoritative weapon state;
- only a confirmed `PhysicsWorld` hit can expose an impacted body handle to the weapon terminal state;
- confirmed impact consumes the torpedo into `MovementDomain::Spent`, emits one bounded damage event and one authoritative explosion event;
- M5 combat integrity is deliberately coarse and body-bound; it is not the M6 compartment/flooding/system-damage model;
- combat damage is SimulationTime-ordered, target-body matched and clamped to a deterministic destroyed state.

`DeepRunM5WeaponRuntimeTests` now also executes a real Jolt-backed sweep against physical static bodies, verifies launch-platform filtering, torpedo impact/consumption, explosion production, basic integrity damage/destruction, target mismatch rejection and time-reversal rejection.

## Next slice

After M5-D acceptance, continue with the remaining Combat Playground dependencies (decoy, simple enemy/destroyer behavior and the minimal combat interaction needed to exercise them) while preserving the perceived-world and physics-authority boundaries. Detailed compartment/flooding/system damage remains M6.
