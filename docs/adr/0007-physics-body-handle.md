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
A handle is valid only in the creating `PhysicsWorld`; destroying a body bumps the slot's
generation, so a reused slot can never validate a stale handle.

Public physics types (`PhysicsVector3`, `PhysicsQuaternion`) are separate from asset types;
quaternion component order is x, y, z, w everywhere in DeepRun.

## Reasons

- Jolt remains a backend detail (ADR-0002); no `JPH::*` type crosses the boundary.
- Slot reuse without generation would silently alias old handles to new bodies.
- A bounded slot table with generation is sufficient for C1; a generic handle allocator is not needed yet.

## Consequences

- Body state queries return copies; callers never hold references into Jolt memory.
- Invalid, foreign, and stale handles are recoverable errors, never undefined behavior.
- `PhysicsWorld` stays the only owner of Jolt bodies until world shutdown.
