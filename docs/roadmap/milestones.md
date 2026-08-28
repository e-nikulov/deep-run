# DeepRun Development Milestones

Status: Active

Documentation axis: M — Milestones

The project-wide M/A/D/C taxonomy is defined in `docs/README.md`. M is the only
mandatory sequential axis; A, D, and C specification numbers are independent
and do not imply milestone order or matching numbers.

## Completed milestones

### Milestone 0 - Engine Bootstrap

Status: COMPLETE

#### Goal

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

#### Required work

##### Build

- CMake root project
- CMakePresets.json
- windows-debug preset
- windows-release preset
- compiler warnings
- clear Debug and Release configuration

##### Application

- application startup
- application shutdown
- main loop
- clean subsystem initialization
- clean subsystem shutdown

##### Platform

- Windows platform layer
- Win32 window
- event processing
- timing
- basic filesystem path support

##### Rendering

- D3D12 device
- command queue
- swap chain
- render target
- clear screen
- present
- resize handling
- DXGI debug support in Debug builds where available

Do not implement a full renderer.

##### Input

Support at minimum:

- keyboard
- mouse
- Xbox-compatible controller

Gameplay-facing code must use input actions rather than raw key checks.

For Milestone 0, a minimal debug input mapping is sufficient.

##### Physics

- integrate Jolt
- initialize physics system
- initialize Jolt job system or adapter
- clean shutdown
- simple physics smoke test

Do not implement marine physics yet.

##### Audio

- integrate miniaudio
- initialize audio device
- create DeepRun audio abstraction boundary
- clean shutdown
- optional simple generated or test tone for verification

Do not implement acoustic simulation yet.

##### Debug UI

Integrate Dear ImGui.

Display:

- engine name
- FPS
- frame time
- renderer status
- physics status
- audio status
- controller status

##### Logging

Create basic logging with categories:

- Core
- Platform
- Render
- Physics
- Audio
- Input

Log startup and shutdown of every subsystem.

##### Headless mode

Support:

DeepRun.exe --headless

Headless mode must:

- not initialize D3D12
- not create a window
- initialize non-rendering core services required by tests
- exit cleanly

##### Tests

Create DeepRunTests executable or equivalent test target.

Required tests:

- core startup/shutdown
- deterministic random smoke test
- Jolt physics initialization
- simple rigid body simulation
- audio abstraction construction without gameplay dependencies

##### Smoke test

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

#### Completion criteria

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

#### Explicitly forbidden during Milestone 0

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

### Milestone 1 - Core Engine

Status: COMPLETE

#### Goal

Turn the M0 bootstrap into a modular, reusable engine core while preserving all M0 runtime behavior.

M1 is infrastructure only. It must remain headless-capable and contain no submarine-specific gameplay.

#### Scope

##### Lifecycle and frame state

- explicit engine initialization, update, render, shutdown, and shutdown-request phases
- deterministic subsystem ownership and cleanup
- frame delta time, elapsed time, and frame index
- configurable fixed physics step
- thin application entry point

##### Scene and transform

- lightweight EnTT registry hidden behind the DeepRun Scene API
- generation-safe entity creation, destruction, validity, iteration, and cleanup
- generic Transform and Tag components
- right-handed world coordinates with +X right, +Y up, +Z toward the camera, and XY as the 2.5D plane

##### Assets and configuration

- normalized relative asset identifiers
- root-relative path resolution
- centralized resource ownership and duplicate-load cache
- predictable missing/invalid resource errors
- validated JSON engine configuration without machine-specific paths

##### Input and diagnostics

- frame-stable engine input state for actions, mouse, and gamepad
- disconnected controller as a valid state
- M1 frame, scene, and resource diagnostics

##### Tests and compatibility

- headless tests for lifecycle, timing, scene/entity, transforms, resources, configuration, and input state
- preserve Debug and Release builds
- preserve `--headless` and `--smoke-test`
- preserve D3D12, resize, ImGui, Jolt, miniaudio, keyboard/mouse, XInput, and clean shutdown

#### Completion criteria

M1 is complete only when:

- Debug and Release configure and build succeed
- Debug and Release CTest pass
- Debug and Release headless runs return exit code 0
- Debug and Release windowed smoke runs return exit code 0
- automated smoke runs exercise D3D12 presentation, resize, and clean shutdown
- no submarine gameplay or future rendering systems have been added

#### Explicitly forbidden during Milestone 1

Do not implement:

- submarine movement or physics
- buoyancy, hydrodynamic drag, propulsion, or control surfaces
- ocean, waves, or underwater effects
- sonar or acoustic simulation
- weapons, damage, flooding, crew, AI, missions, or roguelite systems
- production asset pipeline, Blender integration, or complete glTF loading
- final game UI, networking, or Xbox GDK

## Current milestone

### Milestone 2 - Physical Playground

Status: READY

Scope:

- submarine mesh
- submarine rigid body
- basic water plane
- buoyancy
- hydrodynamic drag
- propulsion / thrust
- control surfaces
- depth response
- side-view camera
- controller-driven submarine commands
- basic gamepad haptics
<!-- deeprun-m2-command-boundary:start -->

Design boundary:

- M2 proves physical vessel control only.
- Do not add compartments, flooding management, power allocation, crew management, tactical pause, command queue, sonar combat, or production system-management UI.
- M2 control code must remain compatible with the later command layer by using semantic vessel commands rather than direct transform manipulation.

<!-- deeprun-m2-command-boundary:end -->

<!-- deeprun-renderer-evolution-roadmap:start -->

### Rendering evolution guardrail

The modern renderer direction is architectural guidance, not an expansion of Milestone 2.

M2 implements only the rendering work strictly required by `submarine mesh`:

```text
load the validated prototype GLB
create engine-owned mesh data
upload vertex/index buffers
render the submarine through the classic indexed D3D12 path
support the side-view camera required by M2
```

M2 explicitly does not require:

```text
mesh shaders
meshlets
GPU-driven culling
ExecuteIndirect
Hi-Z occlusion
Render Graph
DirectStorage
virtual texturing
ray tracing
Work Graphs
neural rendering
production Asset Cooker
```

M2 data structures should avoid assumptions that would prevent later LOD/meshlet metadata,
but unused future subsystems must not be implemented speculatively.

M3 may introduce a small Render Graph when the environment pipeline has enough real passes
to justify it.

GPU-driven submission, meshlets and mesh shaders are later benchmark-gated renderer work.
They should be introduced against representative content rather than assigned to an arbitrary
milestone solely because the APIs exist.

The renderer must preserve a compatibility indexed path while that path materially expands
the supported PC hardware population at acceptable maintenance cost.

<!-- deeprun-renderer-evolution-roadmap:end -->
## Future milestones

### Milestone 3 - Underwater Environment

Future work:

- underwater fog
- depth lighting
- particles
- Gerstner ocean
- floating body wave response
<!-- deeprun-m3-command-boundary:start -->

Design boundary:

- M3 is an environment/presentation milestone.
- Do not pull future submarine-management systems forward merely to populate the environment.
- Environment work may expose debug data needed by later acoustics, but does not introduce the commander command layer.

<!-- deeprun-m3-command-boundary:end -->

### Milestone 4 - Acoustic Playground

Milestone 4 remains a bounded playground for proving the first acoustic
perception vertical slice.
<!-- deeprun-m4-command-note:start -->

Command-layer relevance:

- M4 establishes the player-knowledge boundary: the player sees observations, contacts, tracks, uncertainty, and confidence rather than omniscient target truth.
- A minimal read-only tactical presentation is allowed when needed to make the acoustic slice playable and debuggable.
- Do not introduce power management, compartments, crew, tactical pause, or a generic command queue in M4.

<!-- deeprun-m4-command-note:end -->


#### Core

- AcousticWorld
- AcousticEmitter
- AcousticReceiver
- coarse spectral representation

#### Propagation

- distance attenuation / transmission loss
- propagation delay
- terrain attenuation / occlusion approximation
- basic thermocline
- ambient noise
- self-noise

#### Passive sonar

- passive acoustic observations
- SNR
- bearing uncertainty

#### Active sonar

- active pulse
- outbound propagation
- reflection
- return propagation
- round-trip latency
- detection of outgoing active transmission

#### Minimal perception

- SensorObservation / AcousticObservation integration
- Contact
- basic TrackManager
- track confidence
- track ageing / coasting

#### Signature integration

- cavitation acoustic signature

#### Debug

- AcousticDebugger
- ground-truth versus observed / estimated state

Secondary or later Milestone 4 scope may include:

- surface reflection
- bottom reflection
- bounded multipath
- reverberation envelope
- synthetic biological acoustic source

Hydrodynamic wake is not required for the first working Milestone 4 vertical
slice. Synthetic/debug emitters and reflectors should be used where practical;
torpedoes, destroyers, explosions and full combat remain in Milestone 5.

### Milestone 5 - Combat Playground

Future work:

- destroyer
- torpedo
- decoy
- mine
- explosions
- basic damage
<!-- deeprun-m5-command-note:start -->

Commander interaction added in M5:

- weapon readiness / preparation state
- targeting based on Contact / Track quality rather than omniscient truth
- minimal combat command sequencing
- tactical pause if combat playtesting shows it improves decision-making
- minimal queued gameplay commands only if tactical pause requires them
- combat UI sufficient to inspect track, weapon readiness, threat, and issued orders

Do not turn M5 into the complete submarine-management milestone.

<!-- deeprun-m5-command-note:end -->

### Milestone 6 - Submarine Systems

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
<!-- deeprun-m6-command-note:start -->

M6 is the main submarine command-layer milestone.

Required systemic goals:

- compartments and meaningful watertight boundaries
- flooding coupled to vessel state at gameplay-relevant fidelity
- pumps with power and acoustic consequences
- reactor / available-power model at gameplay abstraction level
- power prioritization / allocation
- local system damage and loss of capability
- crew represented primarily as teams / roles, not individual room-clicking sprites
- damage-control assignments and repair
- system state that is authoritative outside UI
- controller-first systems / damage-control UI
- cross-system consequences between power, noise, sonar, propulsion, flooding, weapons, and repair

The goal is not maximum simulation detail. The goal is constrained commander decisions.

<!-- deeprun-m6-command-note:end -->

### Milestone 7 - Cascading Failure Scenario

Future goal:

Create the first complete systemic emergency sequence involving detection, attack, damage, flooding, power loss, crew reassignment, damage control, and escape.
<!-- deeprun-m7-command-note:start -->

Acceptance intent:

This milestone must prove that the systems create a decision chain rather than a scripted cutscene.

A representative sequence is:

```text
uncertain hostile contact
    -> detection / attack
    -> torpedo hit
    -> local breach
    -> flooding
    -> loss or shortage of power
    -> pump / repair decision
    -> increased acoustic exposure
    -> crew reassignment
    -> escape / continued fight decision
```

If the player can understand why each consequence happened and can choose between
multiple costly responses, the core systemic gameplay is working.

<!-- deeprun-m7-command-note:end -->

### Milestone 8 - Roguelite Layer

Future work:

- seeded runs
- procedural route
- encounters
- events
- rewards
- upgrades
- run summary
<!-- deeprun-m8-command-note:start -->

The roguelite layer must wrap the already-proven tactical/systemic loop.

Route and encounter choices should create commander-level trade-offs such as:

```text
risk versus reward
damage versus continuation
repair versus upgrade
information versus exposure
mission objective versus survival
```

Do not use procedural structure to compensate for an unproven core encounter loop.

<!-- deeprun-m8-command-note:end -->

### Milestone 9 - Advanced Warfare

Future work:

- helicopters
- aircraft
- sonobuoys
- missile launch
- underwater to air transition
- carrier group
- tactical group AI
