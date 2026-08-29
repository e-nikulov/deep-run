# ADR-0008: Submarine rigid-body integration and physics-to-render synchronization

## Status

Accepted (M2 Slice C2)

## Decision

The canonical submarine is a dynamic Jolt box body created by `Game::PhysicalPlayground` through the public
`PhysicsWorld` API. Rendering reads one `GetBodyState(handle)` value copy per frame, after the engine's fixed
step and before any D3D12 work; that single snapshot feeds all node draws:

```text
modelToWorld = BuildBodyToWorld(state) * T(-boundsCenter)
PrepareModelDraws(model, modelToWorld) -> 4 node instances -> DrawModel
```

- **Pivot contract.** The C1 box shape is centered on the body origin, so the body origin is the `ModelAsset`
  bounds center `(min + max) / 2`, not the asset origin. The model-to-body correction is
  `T(-boundsCenter)`; with the initial identity orientation the first visual frame is exactly B2.1
  (`T(c) * T(-c) = identity`).
- **Collision proxy.** A single box from `ModelAsset::bounds` (half extents = size / 2). No convex hull,
  per-fin shapes, compound shape, or collision cooker in M2.
- **Mass.** Game-owned placeholder `M2PrototypeMassKg = 12'000'000.0F`: gameplay/prototype tuning only, not a
  neutral-buoyancy calibration and not derived from classified or precise real vessel data. Free-fall motion is
  mass-independent; the mass / displaced-water relationship is calibrated in the buoyancy slice. The constant
  never moves into generic `PhysicsWorld`.
- **Body configuration.** gravity enabled, zero linear/angular damping, zero initial velocities, identity
  orientation. No water resistance or upward force: falling freely under gravity is the expected C2 state.
- **Camera.** The B2.1 fixed 600 m orthographic side view keeps its target at the INITIAL world center during
  C2 verification so the fall is visible; transformed world bounds (8 corners of the model AABB) set near/far
  only and never change the horizontal zoom. Camera follow/smoothing is deferred until a water-plane reference
  exists.
- **Engine access.** `Engine::Physics()` is a minimal generic accessor to the already-existing subsystem. The
  Engine owns the `PhysicsWorld` and outlives all playground rendering during `Application::Run`; the
  playground stores a non-owning pointer plus a non-owning `PhysicsBodyHandle`. No shared ownership, no new
  game framework, no fixed-step callback: the single simulation path stays `Engine -> PhysicsWorld::Step(1/60)`.

## Reasons

- One snapshot per frame keeps rendering deterministic and prevents Jolt locks/references from surviving into
  D3D12 work.
- The pivot contract makes physics activation visually seamless (no model jump) while keeping the body origin
  meaningful for future buoyancy points.
- A bounds-derived box is the smallest collision proxy that satisfies M2; richer shapes are a later, separate
  decision.

## Consequences

- `Game` owns all Physics <-> Render composition; `Engine/Core`, `Engine/Render`, and `PhysicsWorld` stay free
  of submarine knowledge (enforced by architecture tests).
- The body is owned until `PhysicsWorld` shutdown; the current Application does not require runtime playground
  unload, so no shutdown callback framework is introduced.
- Pure conversion helpers (`BuildBodyToWorld`, `TransformBounds`, bounds center/validation) live in
  `Game/PhysicsRenderSync.*` and are unit-tested without Jolt or D3D12.
