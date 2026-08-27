# ADR-0002: Jolt Physics

## Status

Accepted

## Decision

Use Jolt Physics as the generic rigid-body physics backend.

Jolt must be hidden behind Engine/Physics.

## Jolt responsibilities

- rigid bodies
- collision detection
- CCD
- constraints
- ray casts
- shape casts
- contact generation

## DeepRun responsibilities

Jolt will not own:

- buoyancy
- hydrodynamic drag
- cavitation
- water pressure
- flooding
- sonar
- weapon guidance

## Consequences

Gameplay and Simulation code must not expose Jolt types in public interfaces.