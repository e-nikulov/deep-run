# DeepRun Development Milestones

Status: Active

## Current milestone

Milestone 0 - Engine Bootstrap

Status: NOT STARTED

## Milestone 0 goal

Produce the first minimal DeepRun executable and establish the technical foundation of the engine.

At the end of this milestone the project must have:

- a working CMake build
- a Windows executable
- a Direct3D 12 device
- a window and swap chain
- basic input
- Jolt initialized
- miniaudio initialized
- Dear ImGui debug UI
- logging
- headless mode
- basic automated tests

No submarine gameplay is required yet.

## Milestone 0 required work

### Build

- CMake root project
- CMakePresets.json
- windows-debug preset
- windows-release preset
- compiler warnings
- clear Debug and Release configuration

### Application

- application startup
- application shutdown
- main loop
- clean subsystem initialization
- clean subsystem shutdown

### Platform

- Windows platform layer
- Win32 window
- event processing
- timing
- basic filesystem path support

### Rendering

- D3D12 device
- command queue
- swap chain
- render target
- clear screen
- present
- resize handling
- DXGI debug support in Debug builds where available

Do not implement a full renderer.

### Input

Support at minimum:

- keyboard
- mouse
- Xbox-compatible controller

Gameplay-facing code must use input actions rather than raw key checks.

For Milestone 0, a minimal debug input mapping is sufficient.

### Physics

- integrate Jolt
- initialize physics system
- initialize Jolt job system or adapter
- clean shutdown
- simple physics smoke test

Do not implement marine physics yet.

### Audio

- integrate miniaudio
- initialize audio device
- create DeepRun audio abstraction boundary
- clean shutdown
- optional simple generated or test tone for verification

Do not implement acoustic simulation yet.

### Debug UI

Integrate Dear ImGui.

Display:

- engine name
- FPS
- frame time
- renderer status
- physics status
- audio status
- controller status

### Logging

Create basic logging with categories:

- Core
- Platform
- Render
- Physics
- Audio
- Input

Log startup and shutdown of every subsystem.

### Headless mode

Support:

DeepRun.exe --headless

Headless mode must:

- not initialize D3D12
- not create a window
- initialize non-rendering core services required by tests
- exit cleanly

### Tests

Create DeepRunTests executable or equivalent test target.

Required tests:

- core startup/shutdown
- deterministic random smoke test
- Jolt physics initialization
- simple rigid body simulation
- audio abstraction construction without gameplay dependencies

### Smoke test

Normal mode:

1. start DeepRun
2. create window
3. initialize D3D12
4. initialize Jolt
5. initialize miniaudio
6. show ImGui debug panel
7. run until user closes window
8. shutdown cleanly

Headless mode:

1. start DeepRun --headless
2. initialize core
3. initialize Jolt
4. run a short physics simulation
5. shutdown cleanly
6. return exit code 0

## Milestone 0 completion criteria

Milestone 0 is complete only when:

- cmake configure succeeds
- Debug build succeeds
- Release build succeeds
- tests pass
- normal executable starts
- window renders and presents frames
- resize works
- controller can be detected
- Jolt initialization is verified
- miniaudio initialization is verified
- headless smoke test returns exit code 0
- shutdown produces no known resource lifetime errors

## Explicitly forbidden during Milestone 0

Do not implement:

- submarine physics
- buoyancy
- hydrodynamic drag
- ocean
- waves
- sonar
- acoustic propagation
- cavitation
- torpedoes
- missiles
- flooding
- compartments
- crew
- ship systems
- AI
- roguelite map
- game UI
- asset cooker
- scene editor
- generic scripting
- Xbox GDK

Do not create placeholder implementations for these systems.

## Milestone 1

Physical Playground

Future work:

- submarine rigid body
- buoyancy
- hydrodynamic drag
- propulsion
- control surfaces
- side-view camera
- basic water plane

Do not start until Milestone 0 is complete.

## Milestone 2

Underwater Environment

Future work:

- underwater fog
- depth lighting
- particles
- Gerstner ocean
- floating body wave response

## Milestone 3

Acoustic Playground

Future work:

- acoustic emitters
- passive sonar
- active sonar
- SNR
- distance attenuation
- terrain occlusion
- thermocline
- cavitation signature

## Milestone 4

Combat Playground

Future work:

- destroyer
- torpedo
- decoy
- mine
- explosions
- basic damage

## Milestone 5

Submarine Systems

Future work:

- compartments
- flooding
- pumps
- reactor
- power distribution
- system damage
- crew
- repairs
- lighting failures

## Milestone 6

Cascading Failure Scenario

Future goal:

Create the first complete systemic emergency sequence involving detection, attack, damage, flooding, power loss, crew reassignment, damage control, and escape.

## Milestone 7

Roguelite Layer

Future work:

- seeded runs
- procedural route
- encounters
- events
- rewards
- upgrades
- run summary

## Milestone 8

Advanced Warfare

Future work:

- helicopters
- aircraft
- sonobuoys
- missile launch
- underwater to air transition
- carrier group
- tactical group AI