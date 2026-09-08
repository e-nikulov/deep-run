# Milestone 5 — Combat Playground

Status: IN PROGRESS

Milestone 5 begins from the accepted M4 perceived-world boundary. Combat systems must consume observations/contacts/tracks appropriate to their role and must not gain authoritative hostile entity state merely because Simulation knows it.

## M5-A — weapon readiness and track-constrained targeting

Status: IMPLEMENTED, awaiting CI acceptance.

The first bounded slice establishes an authoritative weapon runtime without adding torpedo movement, damage, destroyers, decoys, mines, explosions, or combat AI yet.

Implemented contract:

- `WeaponDefinition` owns authored preparation time and minimum targeting quality.
- `WeaponRuntimeState` owns the authoritative phase and advances on monotonic `SimulationTime`.
- Initial phases are deliberately bounded to `Stored -> Preparing -> Ready -> Launched` for this slice.
- target assignment consumes `Perception::Track` only.
- runtime target state stores only `trackId`; no target entity handle, `Transform`, or other hostile ground-truth identity is accepted or retained.
- the first conventional-heavyweight targeting profile can require an estimated track position, minimum confidence, bounded bearing uncertainty, and an explicitly accepted lifecycle.
- a normal M4 passive bearing-only track therefore remains valid perceived evidence but is insufficient for a position-requiring torpedo launch solution.
- coasting tracks are rejected unless a weapon definition explicitly opts in.
- launch requires both authoritative `Ready` state and an accepted perceived-world track.

`DeepRunM5WeaponRuntimeTests` is a headless CTest target covering definition validation, readiness timing, SimulationTime reversal rejection, bearing-only/weak/uncertain/coasting target rejection, successful qualified-track assignment, launch gating, and the no-ground-truth target-state boundary.

## Deferred after M5-A

The next slices may add the first conventional heavyweight torpedo runtime movement/guidance and a bounded combat target scenario. Destroyer behavior, decoy interaction, mines, explosions, basic damage, and simple combat AI remain M5 work but are not part of M5-A.
