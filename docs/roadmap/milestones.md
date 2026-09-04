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
- preserve D3D12, resize, ImGui, Jolt, miniaudio, keyboard/mouse, the then-current XInput backend,
  and clean shutdown

The M1 XInput reference is historical. The accepted current Windows controller backend is
`Windows.Gaming.Input` behind the private Windows input backend; it superseded XInput during M2, and
the current controls/input contract remains authoritative.

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

## Completed Milestone 2 - Physical Playground

Status: COMPLETE

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

Accepted slices (in order):

| Slice | Content | Status |
|---|---|---|
| A | GLB CPU asset loading into `ModelAsset` | ACCEPTED |
| A.1 | Material transport through the asset pipeline | ACCEPTED |
| B1 | Indexed GPU geometry layout and upload | ACCEPTED |
| B1.1 | Composition + GPU identity (distinct handles per upload) | ACCEPTED |
| B2 | Indexed submarine rendering (4 nodes / 4 draws / 1632 indices) | ACCEPTED |
| B2.1 | Fixed 600 m gameplay camera, orthographic side view | ACCEPTED |
| C1 | Generic dynamic rigid-body API (`CreateDynamicBoxBody`) | ACCEPTED |
| C1.1 | Opaque physics-handle contract cleanup (generation, foreign/stale rejection) | ACCEPTED |
| C2 | Submarine rigid-body integration + physics-to-render synchronization (ADR-0008) | ACCEPTED |
| C2.1 | 2.5D body DOF constraint + world-bounds synchronization | ACCEPTED |
| D1 | Authoritative flat WaterBody + water-level/depth query | ACCEPTED |
| D2 | Visible flat-water cross-section + M2 depth placement | ACCEPTED |
| E1 | Generic force-at-world-position PhysicsWorld API | ACCEPTED |
| E2 | Pure multi-point buoyancy model + force calculation | ACCEPTED |
| E3 | Submarine buoyancy configuration + fixed-step force integration | ACCEPTED |
| F1 | Pure directional hydrodynamic drag force/torque model | ACCEPTED |
| F2 | Submarine hydrodynamic drag integration + generic torque physics API | ACCEPTED |
| G1 | Pure shaft/RPM/thrust propulsion model | ACCEPTED |
| G2 | Submarine propulsion integration + RPM-driven propeller presentation | ACCEPTED |
| H1 | Pure control-surface hydrodynamic force model | ACCEPTED |
| H2 | Submarine control-surface integration + scripted depth/pitch response | ACCEPTED |
| I1 | Semantic Throttle/Depth vessel commands + controller/keyboard integration | ACCEPTED |
| I2 | Basic semantic gamepad haptics | ACCEPTED |

C2 notes: the canonical submarine is rendered from authoritative Jolt body state through a
`PhysicsBodyState` value copy; box collision proxy derived from `ModelAsset::bounds`; pivot contract keeps
the first visual frame identical to B2.1; camera target stays at the initial world center during C2
verification (camera follow is deferred until a water-plane world reference exists).

C2.1 notes: corrective pass only — no new gameplay scope. The M2 submarine body is constrained to the
gameplay plane (translation X/Y + rotation Z) through a DeepRun-owned `PhysicsDegreesOfFreedom` on the
generic dynamic-body creation contract; rendered world bounds now use the same `modelToWorld` as draw
preparation (ADR-0008).

D2 notes: sea level Y=0, initial submarine center depth = 100 m below the authoritative surface. The
playground owns its scenario `WaterBody` value and derives all presentation from it; the flat-water
cross-section is a temporary M2 presentation path (generic renderer clear-rect below the projected
surface) that M3 may replace without changing WaterBody truth. At completion of D2, buoyancy had not
yet been integrated, so the temporary playground behaviour still allowed the body to sink under gravity
and its signed depth to increase. Later M2 slices E2/E3 superseded this temporary behaviour with the
accepted buoyancy model and integration.

<!-- deeprun-m2-command-boundary:start -->

Design boundary:

- M2 proves physical vessel control only.
- Do not add compartments, flooding management, power allocation, crew management, tactical pause, command queue, sonar combat, or production system-management UI.
- M2 control code must remain compatible with the later command layer by using semantic vessel commands rather than direct transform manipulation.

<!-- deeprun-m2-command-boundary:end -->

<!-- deeprun-renderer-evolution-roadmap:start -->

### Rendering evolution guardrail

The modern renderer direction is architectural guidance, not an expansion of Milestone 2.

All milestones must respect the canonical performance, reference PC, world-scale,
memory and simulation-tier contract in `docs/architecture/engine-spec.md`. Those
engineering budgets become acceptance constraints when representative content for
a milestone exists; they do not add implementation scope to that milestone.

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
giant-world streaming
origin rebasing
advanced texture streaming
production acoustic simulation
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
## Current milestone

### Milestone 3 - Underwater Environment

Status: READY

Implementation status:

| Slice | Content | Status |
|---|---|---|
| A | Scene-linear HDR intermediate plus deterministic SDR tone mapping | ACCEPTED |
| A.1 | Capability-gated real HDR display output | ACCEPTED |
| B | Environment geometry foundation | NOT STARTED |

M3-A uses a renderer-owned `R16G16B16A16_FLOAT` `SceneColorHDR` target. Scene draws write linear values;
a fullscreen renderer pass applies a fixed-exposure (1.0), per-channel Reinhard shoulder and the one manual
linear-to-sRGB transfer into the existing `R8G8B8A8_UNORM` SDR swap chain. Dear ImGui remains an SDR/debug
overlay after tone mapping. This does not implement HDR monitor output, exposure controls, final HDR-aware UI
composition, or other M3 environment systems.

M3-A.1 adds an opt-in `renderer.hdr` request (default `false`) that remains presentation-owned. When the
window's current DXGI output reports active Advanced Color/HDR through `IDXGIOutput6::GetDesc1` (for example,
`RGB_FULL_G2084_NONE_P2020` with 10 bits per color), and an FP16 swap chain reports scRGB Present support,
the renderer uses `R16G16B16A16_FLOAT` with `RGB_FULL_G10_NONE_P709`. The output report describes the active
Windows HDR presentation characteristics; it does not mean that the physical panel itself "is PQ". Windows/DWM
owns Advanced Color composition/output conversion from Deep Run's linear scRGB presentation space toward that
active HDR display path. Deep Run does not own or assume final physical-link/display encoding. Otherwise it logs
the reason and retains the accepted
`R8G8B8A8_UNORM` / `RGB_FULL_G22_NONE_P709` SDR path. Both modes retain the same
`R16G16B16A16_FLOAT` SceneColorHDR intermediate. HDR mode maps scene-linear output directly to linear scRGB;
its temporary fixed reference white is 80 nits (scRGB 1.0) and it uses a bounded 1,000-nit engineering
shoulder. HDR10/PQ application output path, HDR10 metadata, automatic exposure, calibration UI, final
shipping HDR UI composition, runtime SDR/HDR output-mode switching, monitor hot-plug/dynamic output
refresh, and FP16 bandwidth profiling are deferred.

Future work:

- underwater fog
- depth lighting
- bounded scene-linear HDR intermediate, tone mapping, SDR fallback, and
  capability-gated HDR output foundation
- seabed and representative rock formations
- underwater ridges, cliffs, drop-offs, trenches, and other large terrain
  formations needed by the environment slice
- basic underwater ice / ice-shelf / iceberg geometry where appropriate
- world/environment data with independent render, coarse-collision, navigation,
  and future acoustic-query representations
- stable chunk-compatible environment IDs/bounds without requiring production
  streaming
- minimal instanced/batched presentation flora such as kelp, seaweed,
  environmentally appropriate seagrass, and benthic growth
- optional cheap presentation fauna such as fish schools or surface birds when
  useful to the environment slice
- particles
- Gerstner ocean
- floating body wave response
<!-- deeprun-m3-command-boundary:start -->

Design boundary:

- M3 is an environment/presentation milestone.
- M3 preserves `WaterBody` as authoritative water truth; visual waves do not
  redefine gameplay depth.
- M3 does not require full CFD, a particle ocean, production world streaming,
  complex animal AI, or a speculative Asset Cooker.
- Do not pull future submarine-management systems forward merely to populate the environment.
- Environment work may expose debug data needed by later acoustics, but does not introduce the commander command layer.
- Do not add combat, tactical AI, weapon runtime, flooding, crew, or system
  management to M3.

<!-- deeprun-m3-command-boundary:end -->

## Future milestones

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

Gameplay-relevant whale, sperm-whale, or dolphin emissions may begin here as
synthetic/data-driven sources. They must produce uncertain observations and
contacts through the ordinary acoustic/perceived-world pipeline; full animal AI
is not required.

Hydrodynamic wake is not required for the first working Milestone 4 vertical
slice. Synthetic/debug emitters and reflectors should be used where practical;
torpedoes, destroyers, explosions and full combat remain in Milestone 5.

### Milestone 5 - Combat Playground

Future work:

- destroyer
- first conventional heavyweight torpedo
- decoy
- mine
- explosions
- basic damage
- simple combat AI using observations/contacts/tracks rather than player ground
  truth
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

- compartments and meaningful watertight / fire boundaries
- explicit normal/open versus casualty-isolated boundary state
- authoritative compartment-access graph
- boundary states that can close, leak, jam, or be destroyed
- flooding coupled to vessel state at gameplay-relevant fidelity
- physical access consequences from flooding, pressure, heat, smoke, and structural damage
- local compartment habitability including O2, CO2, smoke / contaminants, heat, and emergency breathing capability at gameplay abstraction level
- rate-based atmosphere / flooding state rather than arbitrary per-second random checks
- pumps with acoustic and operational consequences
- gameplay-relevant electrical generation / distribution without a universal power-mana model
- local system damage and loss of capability
- individual authoritative crew records for gameplay-relevant personnel
- rank, specialty, qualification, availability, health, current compartment, and basic readiness / fatigue
- baseline common damage-control capability plus specialist task requirements
- crew tasks with required qualifications, personnel counts, access, and protective-equipment requirements
- automatic best-available-and-reachable team formation
- optional player override of proposed specialist assignments
- proper, degraded / bypass, and containment-only repair outcomes where appropriate
- crew represented in production UI primarily as teams / roles / watches, not individual room-clicking sprites
- injury / incapacitation / isolation affecting actual ship capability
- seeded deterministic probability for discrete casualty outcomes such as damaged closures, hazardous access injury, and improvised-repair failure
- system state that is authoritative outside UI
- controller-first systems / damage-control UI
- cross-system consequences between qualified crew, access, habitability, readiness, time, noise, sonar, propulsion, flooding, weapons, damage, and local electrical capacity

All continuous M6 processes use `SimulationTime`. Flooding, pumping, atmosphere,
fire/smoke/heat, repair, crew travel/tasks, and readiness progress through
rate/state models rather than arbitrary per-frame random rolls. Seeded
simulation-owned randomness is reserved for justified discrete outcomes.

The goal is not maximum simulation detail.

The goal is constrained commander decisions caused by too many simultaneous
problems for the available qualified and reachable people, safe access routes,
and surviving equipment.

<!-- deeprun-m6-command-note:end -->

### Milestone 7 - Cascading Failure Scenario

Future goal:

Create the first complete systemic emergency sequence involving detection, attack, damage, flooding, power loss, crew reassignment, damage control, and escape.
<!-- deeprun-m7-command-note:start -->

Acceptance intent:

This milestone must prove that the systems create a decision chain rather than
a scripted cutscene.

A representative sequence is:

```text
uncertain hostile contact
    -> detection / attack
    -> torpedo hit
    -> local breach / fire / smoke
    -> flooding
    -> commander establishes a watertight / fire boundary
    -> progressive casualty spread is reduced
    -> crew access graph changes
    -> one specialist or team becomes isolated
    -> local habitability degrades
    -> another casualty requires that specialist
    -> rescue / containment / alternate-specialist decision
    -> proper or improvised repair decision
    -> possible local electrical limitation
    -> increased acoustic exposure from emergency action
    -> escape / continued fight decision
```

The scenario must support outcomes where sealing a boundary is correct even
though it temporarily sacrifices access to crew or equipment.

Re-opening a boundary must be a meaningful risk when flooding, smoke, heat,
pressure, or toxic atmosphere can propagate.

If the player can understand why each consequence happened and can choose
between multiple costly responses, the core systemic gameplay is working.

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
- P700 runtime weapon definition and entity using the accepted C0 content asset
- missile launch and authoritative underwater-to-air transition
- supercavitating/Shkval-inspired weapon
- smaller/lighter torpedo or compact underwater weapon
- dropped ASW weapons and depth-charge-like threats
- advanced mines and specialized ASW threats
- anti-submarine nets and contextual explosive hazards where a scenario needs
  them
- carrier group
- advanced enemy-submarine behaviour
- coordinated tactical-group AI

M9 consumes the generic weapon, movement-domain, transition, acoustic,
perception, and AI boundaries established by A1. It does not make the P700,
Shkval, helicopters, aircraft, sonobuoys, or group AI prerequisites for M5.
