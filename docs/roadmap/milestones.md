# DeepRun Development Milestones

Status: Active

## Completed milestone

Milestone 0 - Engine Bootstrap

Status: COMPLETE

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

## Current milestone

Milestone 1 - Core Engine

Status: COMPLETE

## Milestone 1 goal

Turn the M0 bootstrap into a modular, reusable engine core while preserving all M0 runtime behavior.

M1 is infrastructure only. It must remain headless-capable and contain no submarine-specific gameplay.

## Milestone 1 scope

### Lifecycle and frame state

- explicit engine initialization, update, render, shutdown, and shutdown-request phases
- deterministic subsystem ownership and cleanup
- frame delta time, elapsed time, and frame index
- configurable fixed physics step
- thin application entry point

### Scene and transform

- lightweight EnTT registry hidden behind the DeepRun Scene API
- generation-safe entity creation, destruction, validity, iteration, and cleanup
- generic Transform and Tag components
- right-handed world coordinates with +X right, +Y up, +Z toward the camera, and XY as the 2.5D plane

### Assets and configuration

- normalized relative asset identifiers
- root-relative path resolution
- centralized resource ownership and duplicate-load cache
- predictable missing/invalid resource errors
- validated JSON engine configuration without machine-specific paths

### Input and diagnostics

- frame-stable engine input state for actions, mouse, and gamepad
- disconnected controller as a valid state
- M1 frame, scene, and resource diagnostics

### Tests and compatibility

- headless tests for lifecycle, timing, scene/entity, transforms, resources, configuration, and input state
- preserve Debug and Release builds
- preserve `--headless` and `--smoke-test`
- preserve D3D12, resize, ImGui, Jolt, miniaudio, keyboard/mouse, XInput, and clean shutdown

## Milestone 1 completion criteria

M1 is complete only when:

- Debug and Release configure and build succeed
- Debug and Release CTest pass
- Debug and Release headless runs return exit code 0
- Debug and Release windowed smoke runs return exit code 0
- automated smoke runs exercise D3D12 presentation, resize, and clean shutdown
- no submarine gameplay or future rendering systems have been added

## Explicitly forbidden during Milestone 1

Do not implement:

- submarine movement or physics
- buoyancy, hydrodynamic drag, propulsion, or control surfaces
- ocean, waves, or underwater effects
- sonar or acoustic simulation
- weapons, damage, flooding, crew, AI, missions, or roguelite systems
- production asset pipeline, Blender integration, or complete glTF loading
- final game UI, networking, or Xbox GDK

## Milestone 2

Physical Playground

Future work:

- submarine rigid body
- buoyancy
- hydrodynamic drag
- propulsion
- control surfaces
- side-view camera
- basic water plane

Do not start until Milestone 1 is complete.

## Milestone 3

Underwater Environment

Future work:

- underwater fog
- depth lighting
- particles
- Gerstner ocean
- floating body wave response

## Milestone 4

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

## Milestone 5

Combat Playground

Future work:

- destroyer
- torpedo
- decoy
- mine
- explosions
- basic damage

## Milestone 6

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

## Milestone 7

Cascading Failure Scenario

Future goal:

Create the first complete systemic emergency sequence involving detection, attack, damage, flooding, power loss, crew reassignment, damage control, and escape.

## Milestone 8

Roguelite Layer

Future work:

- seeded runs
- procedural route
- encounters
- events
- rewards
- upgrades
- run summary

## Milestone 9

Advanced Warfare

Future work:

- helicopters
- aircraft
- sonobuoys
- missile launch
- underwater to air transition
- carrier group
- tactical group AI
