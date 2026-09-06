# ADR-0011: Authoritative wave query and explicit phase time

Status: Accepted

## Context

Accepted M3-E used Game-owned visual Gerstner components and PresentationTime. A future floating consumer
needs Simulation truth without coupling Marine to Render or silently changing existing submarine buoyancy.

## Decision

Marine owns the canonical three-component `WaterWaveFieldDefinition`, optional in `WaterBodyConfig`.
`surfaceLevelY` stays mean/reference sea level. `Sample()` retains its exact flat hydrostatic contract.
Only `SampleWaveSurface(position, simulationTimeSeconds)` evaluates the instantaneous surface.

World-X queries invert `X(u,t)` with 48 fixed bisection iterations bracketed by `worldX +/- sum(Q*A)`.
Double evaluation publishes checked float local Y, local signed depth and normalized `(-dY/du,dX/du,0)`.
Validation retains amplitude limits (3 m per component, 4 m combined), positive finite wavelength/frequency,
finite phase, Q in [0,1], and summed horizontal derivative bound strictly below 0.5. Canonical amplitude
remains 2.90 m. Valid queries have fixed stack storage and no heap allocation or GPU access.

Game copies Marine components/reference Y into renderer-neutral parameters. The renderer does not include
Simulation. Engine's `SimulationTimeSeconds()` accumulates only completed fixed physics steps. Game passes
it explicitly to the surface draw; particles keep PresentationTime. GPU constants retain float time and the
existing shader/layout; parity is tolerance-based, not bitwise across CPU/GPU or unbounded time ranges.

## Consequences

No existing buoyancy, drag, collision, optical or acoustic consumer adopts the new query. Disabled waves
retain flat behavior. No forces, floating response, currents, spectra, pause UI or time-compression UI are
introduced. Those consumer changes require their own milestone; M3-F remains not started.
