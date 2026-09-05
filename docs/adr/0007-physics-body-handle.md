# ADR-0007: DeepRun physics body handle

## Status

Accepted (M2 Slice C1)

## Decision

`Engine/Physics` exposes a minimal DeepRun-owned rigid-body API over Jolt:

```text
DynamicBoxBodyCreateInfo -> PhysicsWorld::CreateDynamicBoxBody -> PhysicsBodyHandle
PhysicsWorld::GetBodyState(handle) -> copy of PhysicsBodyState
PhysicsWorld::DestroyBody(handle)  -> handle invalid forever
```

`PhysicsBodyHandle` is an opaque non-owning value: world identity + slot index + generation.
Callers see only `IsValid()` and `operator==`; resolving the internal identity is a backend-only
capability of the owning `PhysicsWorld`.

A handle is valid only in the creating `PhysicsWorld`. In M2 C1 the body slot table is append-only:
destroyed slots become inactive and are not recycled. Destroying a body increments the slot's
generation as a safety invariant, so if slot recycling is introduced later, a stale handle can
never alias a new body without changing public handle semantics.

Public physics types (`PhysicsVector3`, `PhysicsQuaternion`) are separate from asset types;
quaternion component order is x, y, z, w everywhere in DeepRun.

## Reasons

- Jolt remains a backend detail (ADR-0002); no `JPH::*` type crosses the boundary.
- Slot recycling without generation would silently alias old handles to new bodies; keeping
  generation now makes that future change safe and invisible to callers.
- An append-only slot table with generation is sufficient for C1; a generic handle allocator is not needed yet.

## Consequences

- Body state queries return copies; callers never hold references into Jolt memory.
- Invalid, foreign, and stale handles are recoverable errors, never undefined behavior.
- `PhysicsWorld` stays the only owner of Jolt bodies until world shutdown.
- M3-B.1 adds `StaticBoxBodyCreateInfo -> CreateStaticBoxBody` for generic axis-aligned static boxes,
  with the same handle/state/destroy contract. Game owns independent coarse environment descriptions;
  render triangles and GPU handles never supply collision authority. Partial scenario creation destroys
  already-created static bodies; successful bodies live until PhysicsWorld shutdown.
- `PhysicsWorld` exposes transient force-at-world-position application (`AddForceAtWorldPosition`, M2 Slice E1):
  forces are Newtons at a world-space meter position, accumulated by Jolt for the upcoming fixed step and reset
  after that step; off-center application points produce torque through Jolt's own cross product.
- force/torque mutation APIs are dynamic-body operations;
- passing a valid static-body handle is a recoverable InvalidInput error and
  must be rejected before the physics backend is invoked.