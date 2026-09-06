# ADR-0012: Opt-in wave-aware buoyancy for one representative float

Status: Accepted (M3-F)

## Context

M3-E.1 established authoritative free-surface shape queries while preserving accepted flat submarine
hydrostatics. One real fixed-step consumer is needed to validate that the query can drive a rigid body without
silently migrating all Marine users.

## Decision

`BuoyancySystem::Calculate(...)` remains flat/reference-plane hydrostatics through `WaterBody::Sample(...)`.
`CalculateWaveSurface(...)` is a separate explicit operation. It accepts caller-supplied `SimulationTime`,
writes caller-owned reusable `BuoyancyResult` storage, and samples `WaterBody::SampleWaveSurface(...)` for
each point. Point signed depth, normal, fraction, displaced volume and Archimedes force derive from the same
sample. The shared private implementation differs only in its surface sampler.

M3-F's first and only consumer is a Game-owned 1,000 kg surface-float test body. It has two local points at
X +/-1 m, potential volume `2 * mass / density`, and Game-owned Jolt damping/planar DOFs. Game passes the
Engine's beginning-of-step SimulationTime, applies the published forces through `AddForceAtWorldPosition`,
and renders the post-step Jolt snapshot through the existing model path.

## Consequences

The canonical submarine stays on `Calculate(...)`; its buoyancy, drag, control surfaces and initial placement
are unchanged. Marine does not depend on Render, and Render does not depend on Marine. The result is a bounded
hydrostatic approximation, not CFD, a pressure or orbital-velocity model, slamming, added mass, radiation
damping or current simulation. No generic floating-body manager or additional object population is created.
