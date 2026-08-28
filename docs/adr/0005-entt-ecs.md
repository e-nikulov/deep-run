# ADR-0005: EnTT scene registry

## Status

Accepted

## Decision

Use EnTT as the lightweight entity registry behind `Engine/Scene/Scene`.

DeepRun code uses its own `Entity`, `Scene`, and component types. EnTT types are not part of gameplay or Simulation interfaces.

## Reasons

- the engine specification already selects EnTT for entity management
- generation-aware entity validity is required by M1
- a proven registry avoids growing a custom ECS framework
- EnTT remains small and header-only

## Consequences

- EnTT is fetched at the pinned `v3.16.0` tag
- generic engine components live under `Engine/Scene`
- gameplay components will live under `Game`, when a gameplay milestone requires them
- Scene exposes only the minimal create, destroy, validity, component, iteration, and clear operations needed by M1
- entity handles are scoped to the Scene that created them and must not be used with another Scene
