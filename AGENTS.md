# DeepRun Development Rules

This file contains mandatory instructions for AI coding agents working on DeepRun.

## Required reading

Before making architectural or implementation changes, read:

- docs/architecture/engine-spec.md
- docs/roadmap/milestones.md
- relevant files under docs/adr/

## Project goal

DeepRun is a submarine roguelite built on a purpose-built C++ game engine.

The engine exists to support DeepRun.

Do not turn DeepRun Engine into a general-purpose game engine.

## Dependency direction

Allowed:

Game -> Simulation
Game -> Engine
Simulation -> Engine
Engine -> Platform

Forbidden:

Engine -> Simulation
Engine -> Game
Simulation -> Game

## Third-party boundaries

Jolt Physics must be hidden behind Engine/Physics.

Gameplay and Simulation code must not directly include or expose Jolt types.

miniaudio must be hidden behind Engine/Audio.

Gameplay and Simulation code must not directly include or expose miniaudio types.

Dear ImGui is for developer and debug tooling only.

Shipping game UI must not depend on Dear ImGui.

## Rendering

The renderer is Direct3D 12 first.

Do not add Vulkan, OpenGL, Metal, or a generic graphics abstraction unless explicitly requested.

Shaders use HLSL and DXC.

## Platform

The current development platform is Windows x86-64.

Xbox Series X/S is a future target.

Gameplay code must not directly depend on Win32 APIs.

Platform-specific functionality belongs under Engine/Platform.

Do not add Xbox GDK dependencies until explicitly requested.

## Simulation rules

Rendering is never the source of gameplay state.

Examples:

- visual bubbles do not determine cavitation
- visual water does not determine flooding
- particles do not determine explosions
- rendered sonar effects do not determine sonar detection

Simulation owns state.

Rendering visualizes state.

## Acoustics

Audio playback and acoustic simulation are separate systems.

Engine/Audio handles audible playback.

Simulation/Acoustics handles:

- acoustic signatures
- propagation
- sonar
- detection
- thermocline effects
- reflections
- ambient noise

Do not implement sonar logic inside miniaudio or AudioEngine.

## Physics

Jolt handles generic rigid-body physics and collision queries.

DeepRun Simulation handles:

- buoyancy
- hydrodynamic drag
- propulsion
- cavitation
- water pressure
- flooding
- marine-specific behavior

Do not modify Jolt to implement DeepRun gameplay unless there is no reasonable alternative.

## Scope control

Before adding an engine feature, answer:

"What current DeepRun gameplay feature requires this?"

If there is no concrete answer, do not implement it.

Prefer:

1. working gameplay
2. clear architecture
3. debugging support
4. performance
5. physical plausibility
6. generality

## Current milestone

Only work on the active milestone defined in:

docs/roadmap/milestones.md

Do not start future milestones unless explicitly requested.

## Coding rules

Use C++23 unless compatibility requires C++20.

Prefer:

- RAII
- value semantics
- smart pointers for ownership
- std::span
- std::string_view
- enum class
- constexpr
- small focused classes
- explicit ownership
- deterministic cleanup

Avoid:

- global mutable state
- gameplay singletons
- raw owning pointers
- deep inheritance trees
- unnecessary macros
- premature abstraction
- speculative framework code

## Files

Do not create empty placeholder source files for future systems.

Create files only when implementing the corresponding feature.

Do not reorganize the repository without a concrete reason.

## Testing

Every completed milestone must:

- configure successfully
- build successfully
- run tests
- pass its smoke test

Use headless tests wherever rendering is not required.

Do not claim something is verified unless it was actually built or executed.

## Error handling

Programming invariants may use assertions.

Runtime failures such as missing optional content must be handled gracefully where reasonable.

Decorative assets must not crash the game.

## Documentation

When making a significant architectural decision:

1. update the relevant architecture document if necessary
2. add or update an ADR
3. keep the ADR short and focused

## Final agent report

After implementation, report only:

- IMPLEMENTED
- VERIFIED
- FILES CHANGED
- KNOWN ISSUES
- NEXT STEP

Do not write a long retrospective unless requested.