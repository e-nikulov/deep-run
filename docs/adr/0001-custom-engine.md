# ADR-0001: Purpose-built DeepRun Engine

## Status

Accepted

## Decision

DeepRun will use a purpose-built C++ engine.

The engine is specialized for DeepRun and is not intended to become a general-purpose game engine.

## Reasons

- direct control over simulation architecture
- future Xbox GDK integration
- specialized marine simulation
- specialized acoustic simulation
- no dependency on a commercial game engine runtime

## Consequences

- higher initial engineering cost
- tooling must remain intentionally minimal
- commodity systems should use proven third-party libraries
- every engine feature must be justified by a DeepRun gameplay requirement