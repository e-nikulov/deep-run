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

## Next slice

M5-B should close the next concrete dependency rather than bypass it: active-sonar observations already carry bounded range evidence, while the current perceived-world track does not yet derive a spatial estimate from that evidence. M5-B should produce a targetable ranged/positioned track inside the perception layer, preserving the no-ground-truth boundary. The first conventional heavyweight torpedo movement/guidance slice follows that perception gate.

Destroyer behavior, decoy interaction, mines, explosions, basic damage, and simple combat AI remain M5 work but are not part of M5-A.
