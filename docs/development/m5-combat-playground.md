# Milestone 5 — Combat Playground

Status: IN PROGRESS

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

## Next slice

M5-C introduces the first conventional-heavyweight torpedo runtime as a bounded headless underwater movement/guidance slice. It will consume only the accepted perceived track identity and spatial estimate, advance on `SimulationTime`, and keep collision, impact, detonation, damage, destroyers, decoys, mines, explosions, and combat AI out of scope until their own slices.
