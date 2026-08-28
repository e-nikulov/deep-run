# ADR-0006: World coordinate system

## Status

Accepted

## Decision

DeepRun uses right-handed world space:

- +X = right
- +Y = up
- +Z = toward the camera

The default side-view camera looks along -Z into the scene.

The primary 2.5D gameplay plane is XY. Z remains available for rendering depth, layering, particles,
camera distance, 3D effects, and future spatial presentation.

Transforms store position, quaternion rotation, and scale. The identity transform has zero position, identity rotation, and unit scale.

## Reasons

- Jolt Physics uses a right-handed coordinate system
- Y-up matches the existing M0 gravity direction
- a documented convention prevents later render and physics conversions from becoming gameplay state

## Consequences

- D3D12 camera and projection code must preserve this world convention
- 2.5D gameplay uses XY as its primary plane, but the engine transform remains three-dimensional
- renderer data never becomes the source of simulation transforms
