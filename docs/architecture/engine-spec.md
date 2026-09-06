# DeepRun Engine — Technical Specification v0.1

Specification: A0 — Engine Architecture Baseline

The project-wide M/A/D/C taxonomy and specification registry are defined in
`docs/README.md`. A0 is an architecture contract, not a milestone or an
implementation-sequence position.

## 1. Назначение

**DeepRun Engine (DRE)** — специализированный C++ игровой движок для разработки 2.5D/3D подводного roguelite `Deep Run`.

Движок НЕ является универсальной заменой Unity/Unreal.

Его задача — максимально хорошо поддерживать конкретный набор механик Deep Run:

* подводное движение;
* физику тяжёлых морских объектов;
* гидродинамику;
* давление воды;
* кавитацию;
* затопление отсеков;
* повреждения систем;
* подводные и надводные взрывы;
* торпеды;
* мины;
* ракеты с переходом `вода → воздух`;
* надводные корабли;
* самолёты и вертолёты;
* морскую поверхность;
* гидроакустику;
* active/passive sonar;
* управление экипажем;
* управление энергией;
* AI противников;
* tactical AI группировки;
* roguelite gameplay;
* Windows;
* будущий Xbox Series X|S.

Главный принцип:

> **Не создавать универсальный engine framework там, где можно создать специализированную систему Deep Run.**

---

# 2. Техническая база

## Язык

**C++23**

Допускается использование возможностей C++20 там, где это улучшает совместимость с библиотеками или Xbox toolchain.

Не использовать нестандартные compiler extensions без необходимости.

---

## Build System

**CMake**

Основные файлы:

```text
CMakeLists.txt
CMakePresets.json
cmake/
```

Основные presets:

```text
windows-debug
windows-release
windows-profile

future:
xbox-debug
xbox-release
```

Основная среда разработки:

```text
Visual Studio 2026
MSVC
Windows SDK
DXC
```

---

# 3. Основные внешние зависимости

Использовать сторонние библиотеки там, где написание собственного решения не создаёт уникального gameplay.

## Physics

**Jolt Physics**

Используется для:

* rigid bodies;
* collision detection;
* CCD;
* constraints;
* ray casts;
* shape casts;
* broadphase;
* contact generation.

Jolt НЕ отвечает непосредственно за:

* buoyancy;
* hydrodynamic drag;
* cavitation;
* flooding;
* pressure;
* sonar;
* torpedo guidance.

Это системы DeepRun.

---

## Audio

**miniaudio**

Используется как низкоуровневый audio backend для:

* audio device;
* playback;
* streaming;
* mixing;
* music;
* UI audio;
* basic 3D spatialization.

Gameplay не должен непосредственно вызывать miniaudio.

Использовать abstraction:

```cpp
AudioEngine
AudioSource
AudioListener
AudioClip
AudioBus
AudioFilter
```

Xbox backend впоследствии может быть заменён на XAudio2/GDK без изменения gameplay.

---

## Debug UI

**Dear ImGui**

Только для:

* debug menus;
* inspectors;
* graphs;
* profiling;
* developer controls;
* runtime tuning.

Не строить shipping UI игры полностью на ImGui.

---

## Entity management

Предпочтительно:

**EnTT**

Использовать для lightweight ECS/entity registry.

Не строить собственный ECS без реальной необходимости.

---

## JSON

Допустимо:

**nlohmann/json**

Использовать для:

* definitions;
* scenes;
* encounters;
* balancing;
* debug configuration.

Позже high-volume runtime assets могут использовать бинарный cooked format.

---

## glTF

Основной 3D asset format:

```text
.glb
```

Для загрузки использовать небольшую библиотеку вроде:

```text
fastgltf
```

или аналогичную.

---

# 4. Высокоуровневая архитектура

```text
DeepRun
│
├── Game
│   │
│   ├── Roguelite
│   ├── Crew
│   ├── Damage
│   ├── Combat
│   ├── Weapons
│   ├── AI
│   └── Missions
│
├── Simulation
│   │
│   ├── Marine Physics
│   ├── Flooding
│   ├── Pressure
│   ├── Acoustics
│   ├── Ocean
│   └── Environment
│
├── Engine
│   │
│   ├── Core
│   ├── Platform
│   ├── Rendering
│   ├── Physics
│   ├── Audio
│   ├── Input
│   ├── Assets
│   ├── UI
│   └── Diagnostics
│
└── Platform
    │
    ├── Windows
    └── Xbox
```

Принцип зависимости:

```text
Game
 ↓
Simulation
 ↓
Engine
 ↓
Platform
```

`Engine` ничего не должен знать о:

* submarine;
* sonar operator;
* torpedo;
* roguelite;
* aircraft carrier.

`Simulation` может знать о физических концепциях:

* water;
* pressure;
* acoustic emitter;
* buoyant body.

`Game` знает уже о:

* submarine;
* crew;
* weapons;
* missions.

---


<!-- deeprun-command-layer-contract:start -->

## Command-layer architecture contract

DeepRun distinguishes direct vessel control from commander-level system
commands.

Direct vessel control is the Milestone 2 path:

```text
semantic input
    -> vessel command state
    -> marine simulation
    -> submarine motion
```

Future commander-level interactions use discrete gameplay commands:

```text
Input / UI
    -> semantic gameplay command
    -> Game-owned command handling
    -> authoritative submarine state
    -> Simulation systems
    -> presentation
```

Production UI must not directly mutate:

```text
submarine transform
flooding state
power allocation
system damage
crew assignment
sonar truth
weapon readiness
```

Authoritative gameplay state must remain independent of rendering and shipping
UI and must be testable headlessly where practical.

Examples of future commands include:

```text
SetPowerPriority
SetSonarMode
SelectContact
PrepareTube
LaunchWeapon
SealBoundary
StartPump
AssignDamageControlTeam
```

These are gameplay concepts and must not become generic `Engine` abstractions
without a concrete current milestone requiring them.

In particular, do not create a generic engine command bus or scripting system
during M2-M4 merely to prepare for future gameplay.

### Tactical pause

DeepRun may use real-time-with-pause for commander-level decisions.

When tactical pause is introduced:

```text
gameplay fixed simulation = frozen
physics / flooding / AI / acoustics / weapons = frozen
UI navigation = active
input navigation = active
allowed gameplay orders may be queued
```

Queued commands must be applied deterministically when simulation resumes,
preferably at fixed-step boundaries.

Tactical pause is separate from presentation/settings menus and is not part of
Milestone 2.

### Player knowledge boundary

The tactical UI must operate on player observations / contacts / tracks rather
than omniscient world truth.

```text
world truth
    -> acoustic / sensor simulation
    -> observation
    -> contact / track
    -> player-facing tactical UI
```

This boundary is authoritative for sonar and future targeting gameplay.

### Cross-system coupling

The submarine command layer is intentionally systemic.

Examples:

```text
more speed
    -> movement benefit
    -> more self-noise / possible cavitation
    -> passive sensing penalty / detection risk

pump activation
    -> flooding benefit
    -> power cost
    -> possible acoustic cost

damage
    -> local equipment loss
    -> flooding / power consequence
    -> changed movement / sonar / weapon capability
```

Exact values are balancing data rather than engine constants.

Detailed gameplay contracts are defined in:

```text
docs/design/submarine-command.md
docs/design/game-loop.md
docs/design/controls.md
```

<!-- deeprun-command-layer-contract:end -->

---
# 5. Структура repository

```text
deep-run/
│
├── CMakeLists.txt
├── CMakePresets.json
├── README.md
├── COPYRIGHT.md
├── THIRD_PARTY_NOTICES.md
├── .gitignore
│
├── cmake/
│   ├── CompilerOptions.cmake
│   ├── Dependencies.cmake
│   ├── Sanitizers.cmake
│   └── Platform.cmake
│
├── Engine/
│   │
│   ├── Core/
│   ├── Platform/
│   ├── Render/
│   ├── Physics/
│   ├── Audio/
│   ├── Input/
│   ├── Assets/
│   ├── Scene/
│   ├── UI/
│   ├── Diagnostics/
│   └── Math/
│
├── Simulation/
│   │
│   ├── Marine/
│   ├── Ocean/
│   ├── Acoustics/
│   ├── Damage/
│   ├── Flooding/
│   └── Environment/
│
├── Game/
│   │
│   ├── Core/
│   ├── Submarine/
│   ├── Crew/
│   ├── Combat/
│   ├── Weapons/
│   ├── AI/
│   ├── Roguelite/
│   ├── Events/
│   └── UI/
│
├── Content/
│   │
│   ├── Models/
│   ├── Textures/
│   ├── Audio/
│   ├── Materials/
│   ├── Scenes/
│   ├── Submarines/
│   ├── Ships/
│   ├── Weapons/
│   ├── Events/
│   ├── Encounters/
│   └── Upgrades/
│
├── Shaders/
│   ├── Common/
│   ├── PBR/
│   ├── Ocean/
│   ├── Underwater/
│   ├── Sonar/
│   ├── Particles/
│   └── UI/
│
├── Tools/
│   ├── AssetCooker/
│   ├── SceneValidator/
│   └── AcousticDebugger/
│
├── Tests/
│   ├── Engine/
│   ├── Simulation/
│   └── Game/
│
└── ThirdParty/
```

---

# 6. Engine/Core

Минимальный набор файлов:

```text
Engine/Core/
├── Application.h
├── Application.cpp
├── Engine.h
├── Engine.cpp
├── EngineConfig.h
├── EngineConfig.cpp
├── GameLoop.h
├── GameLoop.cpp
├── Time.h
├── Time.cpp
├── JobSystem.h
├── JobSystem.cpp
├── EventBus.h
├── EventBus.cpp
├── ServiceRegistry.h
├── ServiceRegistry.cpp
├── Random.h
├── Random.cpp
├── UUID.h
├── UUID.cpp
├── Assert.h
└── Types.h
```

## Application

Отвечает за:

* startup;
* shutdown;
* создание окна;
* создание engine subsystems;
* основной цикл.

---

## Engine

Владеет основными engine services:

```cpp
Renderer
PhysicsWorld
AudioEngine
InputSystem
AssetManager
SceneManager
JobSystem
Diagnostics
```

Для Milestone 1 `Engine` владеет только уже требуемыми runtime services: `PhysicsWorld`, `AudioEngine`,
`InputSystem`, `AssetManager`, active `Scene`, renderer и diagnostics. `JobSystem`, `EventBus`, UUID и
другие перечисленные расширения создаются только тогда, когда их потребует текущая gameplay-механика.
`Application` только разбирает process options и управляет явными фазами `Initialize`, `Update`,
`Render`, shutdown request и `Shutdown`.

---

# 7. Main Loop

Основной цикл:

```text
OS events
 ↓
Input
 ↓
Fixed simulation
 ↓
Gameplay
 ↓
AI
 ↓
Audio update
 ↓
Interpolation
 ↓
Rendering
 ↓
Present
```

Разделить:

```text
FixedUpdate()
Update()
Render()
```

### Fixed simulation

Начальная частота:

```text
60 Hz
```

Milestone 1 хранит accumulator policy в testable `FixedStepAccumulator`; `Engine` использует этот же
utility и передаёт в `PhysicsWorld` только fixed step, а не variable frame delta.

Начиная с M2 Slice E3, каждый реально исполняемый fixed tick имеет единый порядок:

```text
gameplay/simulation fixed update (force production and application)
    -> PhysicsWorld::Step(fixedDeltaSeconds)
```

Ошибка fixed update отменяет этот physics step. Hook остаётся generic точкой композиции: `Game` владеет
marine-specific расчётами, а `Engine` и `PhysicsWorld` о buoyancy не знают.

## Time-domain and time-compression contract

DeepRun distinguishes three clocks:

| Domain | Ownership and permitted use |
|---|---|
| `RealTime` | Monotonic wall/platform time for OS interaction, diagnostics, device handling, and other real-time services |
| `PresentationTime` | Non-authoritative visual, UI, audio, and haptic presentation where deterministic simulation timing is not required |
| `SimulationTime` | Authoritative gameplay time advanced only by the fixed-step simulation scheduler |

M3-E.1 exposes `Engine::SimulationTimeSeconds()`: a double accumulator incremented only after each
completed fixed physics step by its actual float dt. Failed fixed hooks, render-only frames and minimized
suspension do not advance it. Game explicitly supplies this time to Gerstner rendering and Marine CPU
queries; M3-D particles retain the presentation clock. Tactical pause/compression controls are not added.

At normal speed, one simulation second is approximately one real second. Future
player-controlled compression may expose authored choices such as `1x`, `2x`,
`4x`, and `8x`, with a possible higher travel/strategic rate later. Exact rates
and automatic slowdown policy are design/tuning rather than an M3 requirement.

Time compression must advance more fixed simulation steps per unit of
`RealTime`; it must not make the Jolt/authoritative fixed step dangerously large.
All executed steps preserve the canonical fixed-update ordering. Bounded
near/mid/far simulation tiers may update distant or non-critical systems less
often only when their deterministic contracts permit it.

The following use `SimulationTime`: physics integration, submarine movement,
AcousticWorld propagation and scheduled arrivals, sensor integration,
contact/track ageing, persistent signatures and wake decay, AI memory and
decision timers, weapon phases and movement, mines, damage, flooding, pumping,
fire, smoke, heat, O2/CO2, crew work, repairs, fatigue/readiness, and
gameplay-relevant mission timers.

Tactical pause advances zero `SimulationTime`. UI and permitted presentation
may continue, and queued gameplay commands are applied deterministically after
resume at fixed-step boundaries. Audio-device time, render-frame time, and
haptic lifetime are never authoritative gameplay clocks. Time compression does
not imply global audio pitch-shifting or stronger/faster haptics.

Future danger policy may cap or reduce requested compression for authored states
such as an incoming torpedo, terminal weapon phase, collision danger, critical
depth, rapid flooding, major fire, high-confidence hostile engagement, or an
important launch event. The policy consumes authoritative state but does not
change ownership of that state.

This decision is recorded by ADR-0009.

Используется для:

* rigid body physics;
* submarine movement;
* torpedoes;
* flooding;
* pressure;
* damage simulation.

AI может работать реже.

Например:

```text
10 Hz
```

Acoustic simulation:

```text
10–20 Hz
```

Render:

```text
variable
```

целевой:

```text
60 FPS
```

---

# 8. Platform abstraction

```text
Engine/Platform/
├── Platform.h
├── Window.h
├── FileSystem.h
├── Threading.h
├── Gamepad.h
│
├── Windows/
│   ├── WinPlatform.cpp
│   ├── WinWindow.cpp
│   └── WinFileSystem.cpp
│
└── Xbox/
    └── future
```

Нельзя использовать Win32 API непосредственно из gameplay.

---

# 9. Rendering Engine

Основной graphics API:

# Direct3D 12

Не создавать abstraction для Vulkan/Metal/OpenGL.

DeepRun — D3D12-first engine.

The canonical scene-linear HDR/output, capture, environment-presentation, and
production render-asset boundary is specialized in
`docs/architecture/rendering-spec.md` and ADR-0010.

Файлы:

```text
Engine/Render/
├── Renderer.h
├── Renderer.cpp
├── RenderDevice.h
├── RenderDevice.cpp
├── CommandContext.h
├── CommandContext.cpp
├── SwapChain.h
├── SwapChain.cpp
├── DescriptorHeap.h
├── DescriptorHeap.cpp
├── GPUResource.h
├── GPUResource.cpp
├── Buffer.h
├── Texture.h
├── Mesh.h
├── Material.h
├── Shader.h
├── ShaderCompiler.h
├── PipelineState.h
├── Camera.h
├── RenderWorld.h
├── RenderPass.h
├── Light.h
├── ParticleRenderer.h
└── DebugRenderer.h
```

M2 indexed-model draw preparation may accept optional per-node local post-transforms. They are
presentation-only, compose after the immutable node-local transform, and never mutate `ModelAsset`.

---

# 10. Rendering features

MVP renderer должен поддерживать:

* indexed meshes;
* instancing;
* transforms;
* depth buffer;
* perspective camera;
* orthographic camera;
* texture sampling;
* normal maps;
* directional light;
* point lights;
* spot lights;
* shadow mapping;
* fog;
* particles;
* transparency;
* HDR framebuffer;
* post-processing;
* UI overlay.

---

# 11. Shader system

Использовать:

```text
HLSL
DXC
Shader Model 6.x
```

Пример:

```text
Shaders/
├── PBR/
│   ├── PBR.hlsl
│   └── Shadow.hlsl
│
├── Ocean/
│   ├── OceanSurface.hlsl
│   └── OceanFoam.hlsl
│
├── Underwater/
│   ├── Fog.hlsl
│   ├── DepthLight.hlsl
│   └── Caustics.hlsl
│
├── Sonar/
│   └── SonarPulse.hlsl
│
└── Particles/
    ├── Bubble.hlsl
    └── Explosion.hlsl
```

---

<!-- deeprun-rendering-2026:start -->

## Modern rendering architecture contract — 2026 direction

DeepRun remains a Direct3D 12-first engine. The renderer must adopt modern GPU-driven
techniques incrementally, without making experimental APIs or one hardware generation
a prerequisite for the whole game.

### Stable-first toolchain

Production code must use a pinned retail DirectX 12 Agility SDK and a matching retail DXC.

Current reference as of 2026-08-28:

```text
DirectX 12 Agility SDK 1.619.x
Shader Model 6.9 available in retail toolchain
```

This is a reference for dependency updates, not a requirement that every shader compile
to Shader Model 6.9.

Rules:

```text
retail/stable API      -> may become a production dependency
preview API            -> experiments / prototypes only
hardware-specific API  -> optional capability path only
```

Shader Model 6.10, preview Agility SDK branches, experimental neural rendering APIs and
other preview-only features must not become required by shipping gameplay or content.

Each shader pipeline should target only the minimum Shader Model required by that path.

### Renderer capability model

At D3D12 device initialization, build a `RenderCapabilities` snapshot from actual device,
OS, Agility SDK and driver support.

It should expose the capabilities needed by DeepRun, conceptually including:

```text
highest shader model
mesh shader support / tier
enhanced barriers support
variable-rate shading support / tier
sampler feedback support / tier
raytracing support / tier
work graphs support / tier
```

Feature selection must happen at renderer initialization or pass construction.
Do not scatter vendor-name checks or per-draw hardware branching through gameplay code.

### Geometry rendering tiers

DeepRun uses one content representation with multiple rendering paths.

```text
source GLB
    |
    v
validated mesh data
    |
    +--------------------------+
    |                          |
    v                          v
classic indexed data      derived meshlet data
    |                          |
    v                          v
CompatibilityPath         GPU-driven / MeshShader paths
```

#### CompatibilityPath

Required first and retained as a fallback:

```text
indexed vertex/index buffers
VS + PS
instancing
CPU frustum culling where sufficient
ordinary indexed draw calls
```

Milestone 2 starts here.

This path must remain capable of rendering gameplay even when mesh shaders are unavailable.

#### GPUDrivenPath

Introduce only after the basic renderer is proven and profiling shows CPU submission or
visibility work is material:

```text
GPU instance visibility
GPU LOD selection
compute-generated draw arguments
ExecuteIndirect
Hi-Z / hierarchical depth occlusion when justified
```

This is the preferred bridge from the classic renderer to mesh shaders because it moves
visibility and submission work to the GPU without requiring a second asset authoring workflow.

#### MeshShaderPath

On supported hardware, the modern geometry path may use:

```text
meshlets
per-meshlet bounds
normal-cone / backface-cone data where useful
GPU frustum / occlusion / LOD decisions
amplification shader where it produces measurable value
mesh shader rasterization
```

Mesh shaders are an optimization path, not gameplay state and not an artist-authored feature.

The game must never require a different Blender model merely because a different rendering
path is active.

### Meshlet and geometry processing policy

Blender remains the source authoring tool and GLB remains the canonical interchange/runtime
mesh input for the current milestones.

Artists do not author meshlets.

When a production Asset Cooker becomes justified, it may derive:

```text
optimized vertex/index ordering
explicit LODs
meshlets
meshlet bounds / cull data
material references
streaming/page metadata
runtime compression
```

from the same validated source mesh.

For the first implementation, prefer Microsoft DirectXMesh as the offline geometry-processing
candidate because it directly supports DirectX 12 meshlet generation and the future Xbox
toolchain. Keep it out of gameplay and simulation dependencies.

Do not introduce both DirectXMesh and another mesh optimization library unless a benchmark
demonstrates a concrete missing capability or material size/performance win.

### LOD policy

DeepRun does not implement a Nanite clone.

Use conventional LODs first:

```text
LOD0
LOD1
LOD2
LOD3 / impostor where useful
```

Meshlets complement LODs; they do not remove the need for them in the initial renderer.

Hierarchical virtualized geometry, cluster-page streaming and automatic pixel-scale geometry
selection are deferred until real content demonstrates that ordinary LOD + meshlet culling is
insufficient.

### Render graph direction

Milestone 2 does not require a Render Graph.

When Milestone 3 introduces multiple dependent passes such as depth, lighting, fog, particles,
ocean and post-processing, introduce a small DeepRun-specific Render Graph if it reduces
barrier/resource-lifetime complexity.

Its responsibilities may include:

```text
pass dependencies
resource read/write declarations
resource state transitions
transient resource lifetimes
pass culling
graphics/compute queue synchronization
debug visualization
```

It must not become a generic multi-API RHI.

Enhanced Barriers may be used behind this layer when the device reports support. A legacy
barrier path must remain available until the supported hardware baseline makes it unnecessary.

### Material/resource binding direction

Renderer-visible objects should refer to stable engine-owned material/resource handles rather
than owning D3D12 descriptors directly.

Large shader-visible descriptor heaps and direct descriptor-heap indexing may be used by paths
whose Shader Model/device capabilities support them.

Gameplay and Simulation never store raw descriptor indices.

### Lighting path

Milestone 2 uses the smallest forward-lit path needed to display the submarine correctly.

For later underwater scenes, clustered/Forward+ light culling is the preferred first candidate
if many local lights become necessary. Do not implement both full deferred and full Forward+
renderers pre-emptively. Choose the shipping path from GPU captures of representative M3/M5
scenes.

### Texture policy

Production texture preparation should happen offline where practical:

```text
mip chains generated offline
DDS runtime textures
BC7 for high-quality color where appropriate
BC5 for two-channel normal data where appropriate
BC4 / compact formats for scalar masks where appropriate
```

Most assets should use 1K or 2K textures. A 4K texture is an exception justified by a
visible benefit at the asset's actual screen-space size; it is not the default resolution
for every material. Texture authoring and import validation must account for projected
screen size, mipmaps and appropriate compression rather than source resolution alone.

The renderer and asset architecture must leave room for future texture streaming and
residency control, but Milestone 2 does not require those systems.

Virtual texturing / reserved-resource residency is not required until measured VRAM pressure
or content scale justifies it.

### Asset streaming

Milestone 2 does not require DirectStorage.

Once packaged cooked content and loading/streaming become material, DirectStorage may be
introduced behind the Asset/IO boundary.

Production dependencies must use a retail DirectStorage release. Preview compression or asset
conditioning features must remain optional until they become retail and prove useful in a
DeepRun content benchmark.

### Shader and PSO stutter

Known shipping shaders and pipeline state combinations should be prepared and cached
predictably rather than compiled opportunistically during combat.

Advanced Shader Delivery may be evaluated for Windows distribution once deployment work begins,
but gameplay correctness must not depend on that service.

### Optional / research-only rendering features

The following are not baseline DeepRun requirements:

```text
Work Graphs
Shader Model 6.10 preview features
DirectX ML / neural rendering
DXR / path tracing
Shader Execution Reordering
Opacity Micromaps
Variable Rate Shading
vendor-specific frame generation
vendor-specific upscalers
virtualized geometry / Nanite-equivalent system
```

They may be adopted only when all of the following are true:

```text
representative DeepRun workload exists
profiling identifies a relevant bottleneck or visual opportunity
supported hardware population is acceptable
fallback behavior is defined
measured gain is worth implementation and maintenance cost
```

Work Graphs in particular should be evaluated only after the simpler
compute + ExecuteIndirect path exists; do not architect the renderer around Work Graphs in M2-M3.

### GPU-driven does not mean simulation-driven by GPU

The authoritative boundary remains:

```text
Simulation / gameplay state
          |
          v
render snapshot / presentation data
          |
          v
GPU visibility / LOD / rendering
```

GPU culling, meshlets and render graphs may decide what is drawn.
They never decide submarine physics, damage, sonar truth, flooding, AI truth or gameplay state.

<!-- deeprun-rendering-2026:end -->

---
# 12. Material system

Минимальный material model:

```cpp
BaseColor
Metalness
Roughness
Normal
Emission
Opacity
```

Дополнительные flags:

```text
underwater
transparent
unlit
twoSided
```

---

# 13. Asset Manager

Файлы:

```text
Engine/Assets/
├── Asset.h
├── AssetId.h
├── AssetManager.h
├── AssetManager.cpp
├── AssetLoader.h
├── ModelLoader.h
├── TextureLoader.h
├── AudioLoader.h
└── JsonLoader.h
```

API примерно:

```cpp
AssetHandle<Mesh> LoadMesh(path);
AssetHandle<Texture> LoadTexture(path);
AssetHandle<AudioClip> LoadAudio(path);
```

Asset lifetime должен управляться централизованно.

В Milestone 1 runtime asset core поддерживает нормализованные относительные идентификаторы,
root-relative resolution, централизованный cache и загрузку текстовых definitions. Typed loaders для
mesh, texture и audio добавляются вместе с потребляющими их milestones; production cooker здесь не нужен.
`AssetManager` является единственным runtime owner. Caller получает typed `AssetHandle<T>` без shared
ownership; после `Clear()` handle становится invalid. Logical `AssetId` сохраняет регистр, нормализует
separators и safe `.` segments, запрещает `..` escape и не содержит absolute machine path.

Runtime-ready asset files live in asset-specific subdirectories under `Engine/Assets/`; editable source
assets live under `Content/`. C0 uses this split for the generated submarine prototype without adding a
production cooker.

---

# 14. Content formats

Development formats:

```text
Models       .glb
Textures     .png / .dds
Audio        .wav / .flac
Shaders      .hlsl
Definitions  .json
```

For generated 3D content, `.blend` files under `Content/` are editable sources and binary `.glb` files
under `Engine/Assets/` are runtime-ready outputs. The canonical generation and coordinate contract is
defined in `docs/content/asset-pipeline.md`.

## Production submarine content-to-runtime boundary

IG1 -- Production Antey Runtime Integration establishes the minimum production
vessel boundary required before M4. The canonical C0 content details remain in
`docs/content/antey-asset.md`; this section owns only the runtime boundary:

```text
Content/submarines/Antey/
    canonical source-first / accepted production package
    -> bounded deterministic IG1 runtime staging / promotion
    -> Engine/Assets/submarines/Antey/
       runtime-ready GLB + required semantic metadata
    -> AssetManager / content loader
    -> submarine asset definition
    -> submarine runtime entity
    -> future acoustic gameplay / systems
```

Source asset, production asset, and runtime entity are distinct architectural
entities. `Antey_Source.blend`, Blender APIs, derivation scripts, source mesh
hierarchy, and Blender object names exist only on the offline/content-authoring
side. `Antey_Source.blend` is never staged as a runtime asset. Runtime never
loads arbitrary authoring files directly from `Content/`; it consumes only the
staged, already validated production outputs. IG1 creates neither an
`AssetCooker` nor a second authoring truth. Existing production schemas define
the required staged sidecars, rather than a duplicate runtime schema. A source
or GLB node rename is not a gameplay API change when the production semantic
contract is preserved. Runtime and production tooling must not introduce a
dependency on legacy `HP_Antey_*` marker names.

The production submarine asset definition carries a render asset family and
semantic metadata. Its family identity covers LOD0, LOD1, LOD2, and LOD3 even
when an initial runtime path selects only one loaded render variant. Each
registered variant must be independently loadable and validatable. Render-LOD
selection is presentation policy; it cannot alter physics, gameplay, or
simulation state.

The semantic boundary is deliberately small and vessel-specific. It must
represent propeller anchors, torpedo launch anchors, P-700 cells or launcher
geometry, compartments, collision representation, buoyancy representation,
and LOD identity as semantic records. These records are not an implementation
of weapons, flooding, damage, interiors, or sonar. Their identifiers and
transforms survive source mesh-hierarchy changes; raw Blender names and GLB
node names do not cross the runtime boundary as gameplay contracts.

The Assets/import layer may use production GLB internals once to resolve a
semantic record into an opaque runtime render binding or model-node index for
presentation. Game and Simulation retain only the semantic identity and
transform (for example, a propeller semantic ID and anchor transform) plus
authoritative state such as shaft RPM; they neither retain nor look up raw GLB
node names. Existing production-side node-reference fields, if any, remain
private production/import detail and need not be renamed for IG1.

A runtime submarine composes independent transform/motion state, visual asset,
collision representation, buoyancy representation, semantic anchors/metadata,
and submarine physics/gameplay state. This does not prescribe a C++ API, ECS,
or general-purpose asset system. The render model is presentation data, never
authoritative collision geometry. Collision and buoyancy use their separate,
bounded production/runtime representations, so M2/M3 physics never depends on
production visual-mesh detail or selected render LOD.

The existing propulsion state may drive presentation-only propeller rotation
through resolved semantic anchors. Cavitation, acoustic noise, wake VFX,
propeller damage, weapon operation, and other gameplay remain outside IG1.

M4 depends on this boundary rather than asset internals. Future acoustic
systems may receive the submarine transform, velocity, propulsion state,
propeller positions, compartment world positions, and separately defined
sonar-related anchors, but not Blender names, GLB node names, or source
hierarchy:

```text
Production Content -> Runtime Submarine Representation -> Acoustic Gameplay/System
```

В production позже можно добавить Asset Cooker:

```text
source assets
    ↓
AssetCooker
    ↓
optimized runtime assets
```

---

# 15. Scene system

Файлы:

```text
Engine/Scene/
├── World.h
├── World.cpp
├── Entity.h
├── Components.h
├── Scene.h
├── SceneLoader.h
├── TransformSystem.h
└── Prefab.h
```

Минимальные generic components:

```text
Transform
MeshRenderer
Light
Camera
RigidBody
AudioSource
Tag
```

Gameplay components должны жить в `Game/`, а не `Engine/`.

Milestone 1 создаёт только `Transform` и `Tag`: остальные generic components появляются вместе с
соответствующими renderer, physics и audio features. Registry реализован через EnTT и скрыт за API
`Scene`. Entity handle принадлежит создавшей его `Scene` и не может использоваться с другой Scene.
World space является right-handed: +X вправо, +Y вверх, +Z направлен к камере. Default side-view camera
смотрит вдоль -Z в сцену; primary 2.5D gameplay plane — XY. Z используется для render depth, layering,
particles, camera distance, 3D effects и будущего spatial presentation. `Transform` содержит position,
quaternion rotation и scale.

---

# 16. Physics Layer

```text
Engine/Physics/
├── PhysicsWorld.h
├── PhysicsWorld.cpp
├── PhysicsBody.h
├── PhysicsShape.h
├── PhysicsMaterial.h
├── PhysicsQueries.h
├── CollisionLayer.h
├── ContactListener.h
└── JoltBackend/
```

PhysicsWorld оборачивает Jolt.

Gameplay не должен использовать типы Jolt напрямую.

## Canonical physical units and scale

Authoritative DeepRun simulation uses metric units:

```text
1 world unit = 1 metre
distance       metres
velocity       metres / second
acceleration   metres / second^2
mass           kilograms
force          newtons
time           seconds
```

Authored sources may arrive in another scale, but source/import processing must
normalize them into canonical metres and record the conversion. Camera framing,
orthographic width, display resolution, render LOD, and presentation scale never
change authoritative physical dimensions. A 154 m vessel remains 154 m in
simulation. World scale and time scale are independent; physics must not be
shrunk to accelerate traversal.

Render meshes, collision/query proxies, and buoyancy/displaced-volume models are
separate representations. Jolt receives deliberate collision geometry rather
than detailed production render meshes by default.

---

# 17. Marine Physics

Это уже собственная подсистема DeepRun.

```text
Simulation/Marine/
├── WaterBody.h
├── WaterBody.cpp
├── BuoyancyComponent.h
├── BuoyancySystem.h
├── BuoyancySystem.cpp
├── HydroDragComponent.h
├── HydroDragSystem.h
├── PropulsionComponent.h
├── PropulsionSystem.h
├── ControlSurfaceComponent.h
├── ControlSurfaceSystem.h
├── CavitationSystem.h
├── PressureSystem.h
└── MarineEnvironment.h
```

`WaterBody` (M2 Slice D1) — authoritative flat infinite horizontal water body and the source of truth for
buoyancy, depth, pressure and hydrodynamic systems. Rendering is never the source of this state; `WaterBody`
has no knowledge of renderers, physics bodies or submarines. The D1 surface is exactly flat (no time, waves
or currents). M3-E.1 preserves `Sample()` exactly as this flat/reference-plane query even when an optional
Marine `WaterWaveFieldDefinition` is configured. `Config().surfaceLevelY` always means mean/reference Y.
The separate `SampleWaveSurface(position, simulationTimeSeconds)` inverts the horizontally displaced
Gerstner profile and publishes local Y, signed local depth and normalized upward normal. Marine owns the
canonical three components; Game copies them to Render, which never supplies authoritative state.
M3-F adds one explicit opt-in consumer only: the Game-owned representative surface float calls
`BuoyancySystem::CalculateWaveSurface(...)` with beginning-of-step `SimulationTime`, then applies its two
published point forces to its own `PhysicsWorld` handle. `BuoyancySystem::Calculate(...)` remains the exact
flat/reference-plane operation for the canonical submarine; submarine buoyancy, drag, control surfaces,
collision, lighting/fog and acoustics do not become wave-aware. There is no fluid velocity, current, CFD,
pressure or generic floating-body manager. See [ADR-0011](../adr/0011-authoritative-wave-query.md) and
[ADR-0012](../adr/0012-opt-in-wave-buoyancy.md).

M3-G adds a separate fixed Game-owned underwater-flora presentation field. It obtains its terrain contact only
from the existing authored `SeabedProfileConfig` through `SampleSeabedProfileY`; it neither reads generated
render vertices nor introduces a water, wave, Simulation, physics, collision, force, or entity authority.
The 60 deterministic plants remain one immutable opaque model draw, and do not alter the canonical submarine
or M3-F float contracts.

M3-H adds a separate fixed Game-owned ice field. Three stable authored records independently produce one
faceted render model and two deliberate coarse static-box descriptions; render vertices and backend collision
never supply the other representation's authority. A plain mean/reference surface value is composition input
only. Ice has no wave query, clock input, motion, buoyancy, acoustics, destruction, or Simulation entity, and
the canonical submarine and M3-F float retain their existing behavior.

M3-H.1 adds one separate Game-owned presentation fauna field. Twenty-four deterministic fish are baked into
one opaque 168-vertex, 216-index model primitive and uploaded once. Render evaluates one bounded school
translation from the existing Engine `PresentationTime`; the local layout is immutable and no per-fish runtime
state, geometry rebuild, world query, physics body, entity, acoustic state, or gameplay decision is introduced.
The model draw is ordered after ice and before the submarine, M3-F float, and particles. This remains a
presentation-only environment detail; accepted M3-I closes M3 without changing this boundary.

Canonical signed-depth contract:

```text
signedDepthMeters = surfaceLevelY - worldPosition.y
    > 0 -> below the surface (underwater)
    = 0 -> on the surface
    < 0 -> above the water
surface normal = (0, +1, 0); water occupies y < surfaceLevelY
```

For M3-F's explicit dynamic query, each buoyancy point instead uses one coherent local M3-E.1 sample:
`signedDepthMeters`, `surfaceNormal`, point fraction and resulting force all derive from the same
`SampleWaveSurface` call. Force production sees time `t` at the beginning of a fixed step; physics then
integrates `dt`, increments SimulationTime to `t + dt`, and rendering sees that post-step body state alongside
the completed-step Gerstner phase. This is an intentional one-step relationship, not render prediction.

M2 Slice H1 задаёт pure fully-immersed control-surface calculation в неподвижной воде. Один
`ControlSurfaceComponent` представляет одну независимо рассчитываемую поверхность или гидродинамически
объединённую группу и хранит body-local application point и максимальную effective lift area
`Cl_max * referenceArea` в m². Для M2 flow axis — body-local +X, lift axis — body-local +Y:

```text
F_local_y = 0.5 * waterDensity * maxEffectiveLiftArea
            * deflectionFraction * bodyForwardSpeed * abs(bodyForwardSpeed)
```

Density берётся из `WaterBody`; world velocity переводится в body space нормализованной orientation.
Результат содержит world force и world application point. H1 не вычисляет torque, не применяет force и не
моделирует partial immersion, current, angular local flow или propeller wash.

Начиная с M2 Slice H2, Game может компоновать несколько control-surface components на одном vessel. Они
вычисляются из одного beginning-of-tick body snapshot, после чего каждая опубликованная world force
прикладывается в своей опубликованной world position до `PhysicsWorld::Step`. Pitch/heave возникают из
rigid-body dynamics. M2 depth response получается потому, что physical pitch меняет world direction
body-local propulsion axis, а не через прямую установку depth или vertical velocity.

---

# 18. Buoyancy

Объект может иметь несколько buoyancy points в body-local space относительно rigid-body origin. Каждая
точка явно хранит свою долю displaced volume и submersion half-height; сумма point volumes задаёт полный
потенциальный displaced-water volume без отдельного дублирующего total.

```text
bow
stern
port
starboard
center
```

Для каждой world-space точки signed depth берётся только из `WaterBody::Sample`. M2 использует линейную
аппроксимацию частичного погружения:

```text
submergedFraction = clamp((signedDepth + halfHeight) / (2 * halfHeight), 0, 1)
submergedVolume = pointDisplacedVolume * submergedFraction
force = surfaceNormal * waterDensity * gravityMagnitude * submergedVolume
```

Collision proxy volume и displaced-water volume являются независимыми моделями и не выводятся друг из
друга. Приложение рассчитанных point forces к rigid body и получаемые pitch/roll/heave выполняются отдельно.

```text
pitch
roll
heave
```

Без CFD.

---

# 19. Hydrodynamic Drag

M2 F1 задаёт pure directional model для полностью погруженного тела в неподвижной воде. Air drag,
частичное пересечение поверхности, waves и currents в этот contract не входят. `WaterBody` является
единственным источником density.

Linear и angular velocity переводятся из world space в body-local space обратным поворотом нормализованной
orientation. На каждой локальной оси используются независимые coefficients:

```text
linear effective area A = Cd * referenceArea, m^2
angular effective moment K, m^5

F_i = -0.5 * rho * A_i * v_i * abs(v_i)
T_i = -0.5 * rho * K_i * omega_i * abs(omega_i)
```

Body-local force и torque затем поворачиваются обратно в world space. X/Y/Z coefficients разделяют
longitudinal, vertical и lateral/axis-specific response. Расчёт возвращает instantaneous Newton и
Newton-meter values: mass и delta time в формулы не входят, а integration повторно вычисляет drag перед
каждым применением силы. Collision geometry, displaced-water model и drag coefficients остаются
независимыми моделями.

Начиная с M2 Slice F2, Game вычисляет buoyancy и drag из одного authoritative body snapshot в начале
fixed tick, затем применяет опубликованные point forces, net drag force в center of mass и drag torque до
единственного `PhysicsWorld::Step`. Force/torque являются transient inputs и повторно применяются без
умножения на fixed delta time.

Это позволяет лодке ощущаться:

* тяжёлой;
* инерционной;
* устойчивой вдоль корпуса;
* плохо двигающейся боком.

---

# 20. Propulsion

M2 Slice G1 задаёт pure deterministic модель одного независимо симулируемого shaft/propulsor. Один
`PropulsionComponent` содержит положительные ahead/astern RPM и thrust limits, а также RPM/s rates для
spin-up и spin-down; один `PropulsionState` хранит authoritative signed `shaftRpm`. Два винта позднее могут
быть представлены двумя независимыми component/state instances без отдельного manager framework.

```text
requestedDriveFraction:  -1 .. +1
availablePowerFraction:   0 .. 1
effectiveDrive = requestedDriveFraction * availablePowerFraction

ahead targetRpm  = effectiveDrive * maxForwardRpm
astern targetRpm = effectiveDrive * maxReverseRpm
```

RPM меняется за `fixedDeltaSeconds` с конечными spin-up/spin-down rates и без overshoot. При смене знака вал
сначала движется к точному 0 RPM по spin-down rate; обратное вращение может начаться только в следующем
update. Потеря available power задаёт target 0, поэтому существующие RPM и thrust затухают постепенно.

Signed scalar thrust в Newtons вычисляется из нового authoritative RPM по M2 quadratic approximation:

```text
n = shaftRpm / direction-specific maxRpm
thrustNewtons = direction-specific maxThrustNewtons * n * abs(n)
```

`fixedDeltaSeconds` управляет только RPM evolution и не умножает thrust. G1 не выбирает world direction и
не применяет force. `shaftRpm`, а не visual propeller angle, является simulation truth для будущих thrust
integration, cavitation, acoustics и presentation; эти consumers в G1 не реализуются.

Начиная с M2 Slice G2, Game отображает signed shaft thrust на body-local propulsion axis и применяет его в
явной world-space позиции propulsor до `PhysicsWorld::Step`. `shaftRpm` остаётся authoritative simulation
state. Визуальное вращение propeller выводится только из RPM и никогда не передаёт состояние обратно в
simulation.

---

# 21. Cavitation

`CavitationSystem` вычисляет:

```text
depth
water pressure
propeller RPM
propeller condition
speed
```

Результат:

```text
cavitationAmount: 0..1
```

Он влияет на:

```text
acoustic signature
visual bubbles
propulsion efficiency
component wear
```

---

# 22. Water Pressure

Pressure model:

```text
pressure = atmosphericPressure +
           waterDensity * gravity * depth
```

Game uses normalized hull stress.

```text
safe
warning
critical
collapse
```

Значения конкретных игровых лодок хранятся в data definitions.

---

# 23. Flooding Simulation

```text
Simulation/Flooding/
├── Compartment.h
├── CompartmentSystem.h
├── Bulkhead.h
├── Leak.h
├── Pump.h
├── FloodingSystem.h
└── FloodingGraph.h
```

Каждый compartment имеет:

```cpp
volume
waterVolume
airPressure
integrity
mass
```

---

# 24. Flooding graph

Отсеки связаны graph model:

```text
TORPEDO
   │
CONTROL
   │
REACTOR
   │
ENGINE
```

Связи:

```text
door
bulkhead
pipe
vent
```

Вода может перетекать между compartments.

---

# 25. Leak

Каждая пробоина:

```cpp
position
area
depth
externalPressure
internalPressure
```

Определяет:

```text
flowRate
```

При погружении глубже leak rate увеличивается.

---

# 26. Pumps

Насос зависит от:

```text
power
integrity
operator
```

И удаляет:

```text
water volume / second
```

---

# 27. Damage System

```text
Simulation/Damage/
├── Damage.h
├── DamageType.h
├── DamageSystem.h
├── Explosion.h
├── ExplosionSystem.h
├── SystemDamage.h
└── FireSystem.h
```

Типы:

```text
kinetic
explosive
pressure
fire
flooding
electrical
collision
```

---

# 28. Cascading Failures

Это важнейшая gameplay-функция.

Systems должны быть связаны через события.

Например:

```text
Explosion
 ↓
Hull breach
 ↓
Flooding
 ↓
Electrical short
 ↓
Pump failure
 ↓
More flooding
 ↓
Extra mass
 ↓
Loss of depth control
 ↓
Higher pressure
 ↓
Higher leak rate
```

Нельзя hardcode подобные последовательности как сценарий.

Они должны естественно возникать из систем.

---

# 29. Ocean

```text
Simulation/Ocean/
├── OceanSystem.h
├── OceanSurface.h
├── GerstnerOcean.h
├── OceanQuery.h
├── CurrentField.h
└── WaveSpectrum.h
```

Первый вариант:

# Gerstner Waves

Позже:

# FFT Ocean

---

# 30. Ocean Query

CPU simulation должна уметь запросить:

```cpp
GetWaterHeight(x, z);
GetWaterNormal(x, z);
GetCurrent(position);
```

Renderer и physics должны использовать совместимую wave model.

Это особенно важно для:

* destroyers;
* carrier;
* floating mines;
* buoys.

---

# 31. Underwater Environment

```text
Simulation/Environment/
├── WaterVisibility.h
├── DepthLighting.h
├── Thermocline.h
├── CurrentSystem.h
└── WeatherSystem.h
```

Глубина влияет на:

```text
sunlight
color absorption
visibility
pressure
temperature
sonar
```

World/environment ownership, coarse terrain-query participation, flora/fauna
boundaries, and stable authored identifiers are defined in
`docs/architecture/simulation-spec.md`. Rendering consumes that state according
to `docs/architecture/rendering-spec.md`; it is not the environment source of
truth.

---

# 32. Audio Engine

```text
Engine/Audio/
├── AudioEngine.h
├── AudioEngine.cpp
├── AudioDevice.h
├── AudioClip.h
├── AudioSource.h
├── AudioListener.h
├── AudioBus.h
├── AudioMixer.h
├── AudioFilter.h
└── MiniAudioBackend/
```

Основные audio buses:

```text
Master
Music
UI
Environment
Interior
Weapons
Sonar
Voice
```

---

# 33. DeepRun Acoustic Simulation

Gameplay-relevant underwater acoustics belongs to `Simulation/Acoustics/`.
It is separate from audible presentation in `Engine/Audio/`.

This section describes subsystem placement only. The canonical behaviour for
propagation, signatures, sonar and acoustic observations is defined in:

`docs/architecture/acoustics-spec.md`

```text
Simulation/Acoustics/
├── AcousticWorld.h
├── AcousticEmitter.h
├── AcousticReceiver.h
├── AcousticSignature.h
├── AcousticBand.h
├── PropagationModel.h
├── ReflectionModel.h
├── ThermoclineModel.h
├── AmbientNoiseModel.h
├── PassiveSonar.h
├── ActiveSonar.h
├── AcousticObservation.h
└── AcousticDebugger.h
```

These entries are a conceptual layout, not placeholder implementation work.
Contacts and tracks belong to the perceived-state layer described in
`docs/architecture/simulation-spec.md`.

---

# 34. Acoustic Signature

Gameplay-relevant emitters expose data-driven acoustic signatures. A signature
may represent broadband, machinery, propulsion, cavitation and transient
energy using a small number of spectral bands.

The authoritative spectral representation and band boundaries are defined in
`docs/architecture/acoustics-spec.md`; they are tuning data rather than a
second fixed engine contract. Full waveform simulation is not required.

---

# 35. Passive Sonar

Passive sonar produces acoustic observations through this conceptual flow:

```text
received signal
+ noise / masking
-> SNR and observable features
-> AcousticObservation
-> Contact / Track processing
```

SNR remains an acoustic metric. It may influence observation confidence,
bearing uncertainty and classification features, but it does not reveal a
perfect known enemy. Final contacts, classifications and track quality are
owned by the perceived-state layer defined in
`docs/architecture/simulation-spec.md`.

---

# 36. Active Sonar

Active sonar follows this conceptual flow:

```text
Emitter
 ->
outbound propagation
 ->
Reflector
 ->
return propagation
 ->
Receiver
 ->
AcousticObservation
```

The observation preserves round-trip propagation delay. The outgoing ping is
itself observable by other receivers, so a transmitter may reveal itself
before receiving its own echo. Final contacts and tracks belong to the
perceived-state layer.

Reflection, multipath and reverberation behaviour is defined only in
`docs/architecture/acoustics-spec.md`.

---

# 37. Sonar Environment

`AcousticWorld` owns gameplay propagation through the authored underwater
environment, including distance loss, terrain effects, acoustic layers and
ambient noise. Jolt may provide coarse geometry queries but does not own
acoustic propagation.

Detailed environmental, reflection and noise behaviour is canonical only in
`docs/architecture/acoustics-spec.md`.

---

# 38. Acoustic Debugger

The developer-only Acoustic Debugger should explain the observation pipeline,
including:

```text
source
receiver
propagation paths
arrival times
SNR
losses
ambient and self-noise
AcousticObservation
ground-truth versus observed / estimated state
```

Ground-truth identity is available to developer diagnostics, not ordinary
sensor consumers. Detailed diagnostic expectations are defined in the
canonical acoustic and perceived-state specifications.

---

# 39. Weapons system

```text
Game/Weapons/
├── WeaponSystem.h
├── WeaponDefinition.h
├── Torpedo.h
├── TorpedoGuidance.h
├── TorpedoSeeker.h
├── Decoy.h
├── Mine.h
├── Missile.h
├── MissileLauncher.h
└── ExplosionWarhead.h
```

The canonical future weapon-definition/runtime/phase/movement-domain and
knowledge boundaries are defined in `docs/architecture/simulation-spec.md`.
This conceptual file layout is not permission to create placeholders.

---

# 40. Torpedo State Machine

```text
LOADED
↓
LAUNCHED
↓
SAFE_RUN
↓
SEARCH
↓
TRACK
↓
TERMINAL
↓
HIT
```

Guidance system — gameplay system.

Physics — Jolt + marine physics.

Sonar seeker получает `AcousticObservation` от `AcousticWorld` и формирует
собственное targeting knowledge без доступа к hostile ground truth.

---

# 41. Decoys

Decoy становится обычным:

```text
AcousticEmitter
```

с искусственной signature.

Это позволяет torpedo seeker реально выбирать между:

```text
submarine
decoy
noise
```

на основании общей акустической модели.

---

# 42. Missile system

Ракета использует разные movement modes:

```text
IN_LAUNCHER

FLOODED

EJECTION

UNDERWATER

SURFACE_TRANSITION

POWERED_FLIGHT

GUIDANCE

TERMINAL
```

---

# 43. Launcher / Missile Tube

Пусковая шахта имеет state machine:

```text
LOADED
↓
FLOODING
↓
PRESSURE_EQUALIZING
↓
READY
↓
LAUNCH
↓
DRAINING
```

Эти состояния могут быть нарушены повреждениями.

---

# 44. Aircraft

```text
Game/AI/Air/
├── AircraftController.h
├── HelicopterController.h
├── Sonobuoy.h
└── AirSearchPattern.h
```

Не создавать flight simulator.

Достаточно:

```text
position
heading
speed
altitude
turn rate
```

---

# 45. AI Architecture

Не делать neural AI.

Current accepted direction:

```text
Hierarchical State Machines
+
Blackboard
+
Perception
+
Tactical Director
```

This pass preserves the accepted HSM + Blackboard + Perception + Tactical
Director direction. Concrete class names and data layout remain implementation
choices. The canonical future AI contract is data-driven, headless-capable,
explainable, and constrained to perceived knowledge as defined in
`docs/architecture/simulation-spec.md`. Shipping tactical AI must work fully
offline and must not require an LLM.

---

# 46. Unit AI

Например Destroyer:

```text
PATROL

SUSPICIOUS

SEARCH

TRACK

ATTACK

EVADE

LOST_CONTACT
```

---

# 47. Tactical Group AI

Для АУГ:

```text
Game/AI/Tactical/
├── TacticalDirector.h
├── GroupBlackboard.h
├── TaskAssignment.h
└── ContactManager.h
```

Director решает:

```text
which ship investigates
where helicopter searches
when carrier changes course
where sonar buoys are deployed
```

---

# 48. Crew System

```text
Game/Crew/
├── CrewMember.h
├── CrewSystem.h
├── CrewSkill.h
├── CrewAssignment.h
├── CrewTask.h
├── Injury.h
└── Fatigue.h
```

Первый MVP не должен иметь бытовую симуляцию.

Crew влияет на:

```text
sonar
repair
reactor
weapons
damage control
navigation
```

---

# 49. Ship Systems

```text
Game/Submarine/Systems/
├── ReactorSystem.h
├── ElectricalSystem.h
├── PropulsionSystem.h
├── SonarSystem.h
├── WeaponSystem.h
├── PumpSystem.h
├── LifeSupportSystem.h
└── LightingSystem.h
```

---

# 50. Power Grid

Система электроэнергии должна быть отдельным graph.

```text
REACTOR
   ↓
MAIN BUS
├── ENGINE
├── SONAR
├── WEAPONS
├── PUMPS
└── LIGHTING
```

Игрок распределяет ограниченную мощность.

---

# 51. Lighting and failure feedback

LightingSystem получает состояния:

```text
normal
unstable
emergency
blackout
```

Damage events могут вызывать:

```text
light off
flicker
emergency lights
restore
```

Это должно быть связано с electrical simulation, а не только scripted animation.

---

# 52. Input System

```text
Engine/Input/
├── InputSystem.h
├── InputAction.h
├── InputMap.h
├── KeyboardDevice.h
├── MouseDevice.h
└── GamepadDevice.h
```

Gameplay использует actions:

```text
Move
Depth
Throttle
Fire
Sonar
Decoy
SelectContact
Pause
Interact
```

Не использовать физические кнопки непосредственно в gameplay.

## Input architecture contract

Gameplay и Simulation должны работать только с semantic actions и
нормализованными analog axes.

Они не должны зависеть от:

```text
Win32 virtual keys
mouse button constants
platform gamepad button masks
raw platform stick values
raw platform trigger values
```

Основные поддерживаемые устройства:

```text
keyboard
mouse
Xbox-compatible controller
```

Xbox-compatible controller является reference control scheme.

Keyboard + mouse должны предоставлять feature-equivalent gameplay control
на PC.

Canonical semantic gameplay inputs:

```text
Throttle
Depth
AimX
AimY
FireWeapon
PrepareWeapon
ActiveSonarPing
SilentRunning
DeployDecoy
SelectContact
Interact
Cancel
OpenTacticalView
Pause
```

M2 Slice I1 currently implements only the direct vessel analog axes:

```text
InputAxis::Throttle: -1 astern, 0 neutral, +1 ahead
InputAxis::Depth:    -1 surface / nose-up, 0 neutral, +1 dive / nose-down
```

The Input layer owns physical-device normalization, left-stick dead zone, and keyboard/controller-to-semantic
mapping. Game receives stable `InputState` semantic values only, snapshots them into a Game-owned vessel command
for each fixed tick, and never reads raw keys, XInput fields, or device connection state.

Canonical physical bindings определены в:

```text
docs/design/controls.md
```

Input layer отвечает за:

```text
device polling
connection state
disconnect handling
stick normalization
trigger normalization
dead zones
response curves
mapping physical inputs to semantic actions
```

Ожидаемые normalized ranges:

```text
stick axis: -1.0 .. +1.0
trigger:     0.0 .. 1.0
```

Submarine simulation не должна содержать XInput-specific input handling.

Игрок задаёт vessel commands. Input не должен напрямую изменять submarine
position или velocity.

---

## Haptics

M2 Slice I2 implements this presentation-only boundary:

```text
Game semantic HapticEvent
    -> Game haptic feedback mapping
    -> generic Engine haptic mixer
    -> normalized low/high motor output
    -> Windows.Gaming.Input backend
```

Gameplay and Simulation never directly control gamepad motors. The Engine knows only generic effect IDs,
normalized motor magnitudes, presentation duration and integer priority; it has no semantic event names or
marine state.

Submitting the same generic effect ID replaces its amplitudes and refreshes its lifetime. This prevents a
continuous effect emitted on fixed ticks from stacking with itself. Different IDs may coexist. At evaluation,
only the highest active priority participates; lower-priority effects remain active and continue ageing. Effects
at the winning priority add per motor, clamp to `0..1`, and then receive the normalized master intensity.

After input polling and the application timer tick, previous active effects age once using the application-frame
delta. Current fixed simulation then may submit or refresh semantic effects, and resolved normalized vibration is
sent to the backend after fixed simulation. Current-frame submissions are not retroactively aged by the elapsed
frame delta, while a frame with zero fixed steps still ages inherited effects once. This is independent of how
many fixed ticks ran. When globally disabled, output is exact zero while effects continue ageing, so expired
effects never resurrect on re-enable. Effect durations use presentation time and never simulation, physics or
random state.

Windows desktop controller backend uses `Windows.Gaming.Input` through a private
`Engine/Input/Windows` implementation. C++/WinRT and WGI gamepad types do not escape
the Input layer; Game and Simulation continue to receive only normalized semantic axes.

Windows gates WGI gamepad input on foreground focus. While the gameplay HWND is not
foreground, or a current reading is unavailable, the backend publishes a neutral controller
contribution rather than caching the last throttle/depth value. When focus and a current
reading return, the backend uses that new reading.

The generic low/high motor output maps directly to WGI `LeftMotor`/`RightMotor`; M2 leaves
both trigger motors at zero. WGI calls and event subscriptions remain private to the Windows
input implementation.

Haptic system должен поддерживать:

```text
low-frequency motor
high-frequency motor
effect duration
priority
mixing
clamping
master intensity
global enable / disable
safe device disconnect
same-ID replace / refresh
shutdown motor zeroing
```

Motor intensity нормализована в диапазон:

```text
0.0 .. 1.0
```

Haptics является presentation state и не должен влиять на:

```text
physics
AI
acoustic simulation
random state
save state
simulation determinism
headless execution
```

Controller absence is normal presentation state: generic submissions still succeed and the backend degrades to
silence. Ordinary shutdown sends zero to both motors before the input backend is destroyed. Headless execution
does not initialize controller output hardware. Before a minimized window enters its suspended event wait, the
Engine sends exact zero to both motors; mixer effects and simulation remain paused rather than being cleared or
artificially expired.

Semantic haptic events и reference vibration patterns определены в:

```text
docs/design/controls.md
```

---

# 53. UI Engine

Shipping UI не должен зависеть от ImGui.

```text
Engine/UI/
├── Canvas.h
├── Widget.h
├── Panel.h
├── Image.h
├── Text.h
├── Button.h
├── ProgressBar.h
├── Layout.h
├── FocusManager.h
└── UINavigation.h
```

Обязательно поддержать controller-first navigation.

Shipping UI должен быть полностью доступен без мыши.

Baseline controller navigation:

```text
Left Stick / D-pad -> navigate
A                  -> confirm / activate
B                  -> cancel / back
LB / RB            -> tabs / stations
View               -> tactical overview where applicable
Menu               -> pause
```

`FocusManager` и `UINavigation` должны обеспечивать предсказуемый focus
между интерактивными элементами.

Ни один production gameplay screen не должен требовать mouse input.

Mouse является дополнительным PC interaction method, но не отдельной
обязательной gameplay capability.

Reference bindings:

```text
docs/design/controls.md
```

---

# 54. Game UI

```text
Game/UI/
├── HUD/
├── SonarScreen/
├── CrewScreen/
├── DamageControlScreen/
├── MapScreen/
├── EventScreen/
└── RunSummary/
```

---

# 55. Roguelite System

```text
Game/Roguelite/
├── RunManager.h
├── RunState.h
├── RunSeed.h
├── RunMap.h
├── RunNode.h
├── EncounterGenerator.h
├── RewardSystem.h
└── UpgradeSystem.h
```

`RunSeed` должен детерминировать:

```text
map
events
rewards
selected encounter parameters
```

Не пытаться делать Jolt physics полностью deterministic.

---

# 56. Scene types

Минимум:

```text
Boot
MainMenu
RunMap
Encounter
RunSummary
```

---

# 57. Save System

```text
Game/Core/Save/
├── SaveManager.h
├── ProfileSave.h
├── SettingsSave.h
└── SaveVersion.h
```

Save formats должны иметь:

```text
formatVersion
```

и поддерживать миграцию.

---

# 58. Random System

Разделить:

```text
VisualRandom
SimulationRandom
RunRandom
```

`RunRandom` детерминирован seed.

Particles не должны менять результат gameplay.

---

# 59. Job System

```text
Engine/Core/JobSystem.*
```

Thread pool используется для:

* acoustic queries;
* asset loading;
* AI processing;
* particle preparation;
* Jolt jobs.

Main gameplay state нельзя хаотично изменять из worker threads.

---

# 60. Diagnostics

```text
Engine/Diagnostics/
├── Logger.h
├── Profiler.h
├── DebugOverlay.h
├── Console.h
├── Metrics.h
└── CrashHandler.h
```

Debug overlay:

```text
FPS
frame time, median, p95 and p99
CPU main-thread time
CPU render-thread / submission time
GPU frame time
physics time
acoustics time
AI time
draw calls
visible triangles
visible / rendered objects
material and state-change counters where practical
VRAM usage and budget
RAM working set
audio voices
physics bodies
active and simplified simulation entity counts
```

---

# 61. Gameplay Debugging

Дополнительно:

```text
depth
pressure
speed
throttle
RPM
cavitation
noise
sonar SNR
enemy state
flooded volume
total mass
center of mass
center of buoyancy
reactor output
available power
```

As the owning milestones arrive, developer-only diagnostics should also expose
simulation time/scale and fixed-step state; observations, contacts, tracks, and
uncertainty; AI belief/selected state; weapon phase and movement domain;
water/medium transitions; acoustic emitters/paths/noise and terrain queries;
and compartment rates, boundaries, crew access/assignments, habitability,
O2/CO2, repair progress, and local electrical availability. Ground-truth views
remain developer-only.

---

# 62. Debug Draw

Renderer должен уметь рисовать:

```text
lines
rays
spheres
boxes
arrows
text
```

Используется для:

* sonar rays;
* AI paths;
* buoyancy points;
* collision shapes;
* torpedo seeker;
* explosions;
* acoustic propagation.

---

# 63. Developer Console

Runtime console:

```text
spawn destroyer
spawn torpedo
damage engine 50
flood compartment engine 0.5
set depth 500
set sea_state 5
toggle acoustics_debug
godmode
give torpedo 10
```

---

# 64. Testing

Тесты должны запускаться без renderer там, где возможно.

Примеры:

```text
Flooding increases with depth

Closed bulkhead blocks flow

Pump removes water

Cavitation threshold changes with depth

Torpedo prefers stronger acoustic contact

Damage reduces system efficiency

Run map is deterministic for same seed
```

---

# 65. Headless Simulation

Движок должен поддерживать:

```text
DeepRun.exe --headless
```

В headless mode должны работать:

```text
game simulation
physics
AI
acoustics
roguelite
tests
```

Не создаётся D3D12 renderer.

Это критически полезно для Codex и CI.

---

# 66. CI

Минимальный pipeline:

```text
configure
↓
build
↓
unit tests
↓
headless smoke test
```

Smoke test:

```text
start simulation
spawn submarine
run 10 seconds
spawn enemy
fire torpedo
run simulation
exit successfully
```

---

# 67. Performance and world-scale budgets

This section is the canonical A0 contract for frame performance, world scale,
geometry, memory and simulation budgets. It distinguishes hard contracts from
initial engineering targets and guidelines. Profiling may refine an engineering
budget without weakening the architectural boundaries or the measurable 60 FPS
performance target.

## Frame performance contract

Hard contract:

```text
primary performance target   60 FPS sustained gameplay
hard gameplay frame budget   16.67 ms
reference resolution         native 2560x1440
reference benchmark          reference PC defined below
required upscaling           none
```

The secondary baseline is 1920x1080 at 60 FPS. The architecture must not assume
a high-end GPU. Future console constraints, especially Xbox Series S-class
hardware, must be considered architecturally, but no unverified console
performance numbers are part of this contract.

Normal gameplay engineering targets on the reference PC are:

```text
GPU frame time                     <= 12-13 ms
CPU main/render critical path      <= 8-10 ms
```

These CPU and GPU values reserve headroom for workload variation, transients and
future features. They are engineering targets, not promises that every individual
frame has an absolute maximum below those values. The remaining part of the
16.67 ms frame budget must remain headroom rather than being consumed continuously.

Frame pacing must be evaluated using median, p95 and p99 frame times. Recurring
p99 spikes above approximately 20 ms during normal gameplay are regression
candidates. Streaming, resource creation, shader/PSO preparation and content
activation must not regularly create 30-50+ ms stalls during gameplay.

<!-- deeprun-performance-reference-2026:start -->

## Reference PC and performance gate

The primary development benchmark uses this reference PC:

```text
CPU   Ryzen 5 5600X
GPU   Radeon RX 6600 8 GB
RAM   32 GB
OS    Windows 11
API   Direct3D 12
```

The benchmark runs at native 2560x1440 and must sustain the 60 FPS performance
target without requiring temporal or spatial upscaling. Upscaling remains an
optional scalability feature, not a substitute for meeting the reference target.

This machine is a repeatable development and optimization gate, not the final
minimum customer specification. Do not publish final PC minimum/recommended
specifications until representative gameplay and scalability presets exist.

Xbox Series S-class constraints remain a future architecture consideration and
may become a stricter shipping gate after representative GDK profiling is
available. Until then, do not state unverified console performance targets.

<!-- deeprun-performance-reference-2026:end -->

## World scale and representation contract

DeepRun separates three related but different scopes:

```text
logical world       mission, gameplay, strategic and contact state
active simulation   entities evaluated at an appropriate simulation tier
rendered world      GPU-visible representation, including the high-detail zone
```

Logical operational areas must support scales on the order of 100 x 100 km or
larger. This does not require one enormous fully loaded render scene or physics
world. The initial distance engineering budgets are:

```text
0-2 km       full-detail gameplay, rendering and simulation
2-8 km       simplified representation, LOD and reduced update frequency
8 km+        strategic/acoustic/contact representation without a required
             rendered mesh or rigid body
```

Profiling and gameplay testing may refine these radii, but the architecture must
preserve the separation. In particular:

```text
logical world != simultaneously loaded/rendered world
logical world != full-rate physics simulation world
```

A distant ship, submarine or contact may remain in gameplay simulation, mission
logic and the sonar/acoustic model while having no GPU mesh, full-rate physics
body, full-rate animation or expensive per-frame update. Acoustic/contact
representation is separate from visual and physics representation. This is a
natural fit for DeepRun sonar gameplay: a gameplay-relevant distant entity need
not be rendered to exist.

Presentation state never becomes authoritative simulation state. Rendering may
visualize a contact or select a presentation LOD, but it cannot determine whether
that contact exists, is detected or affects gameplay.

## Large-world precision guardrail

The engine architecture must permit origin rebasing or an equivalent
local-coordinate/high-precision large-world strategy. It must not permanently
lock submarine positions, torpedoes, distant contacts or acoustic calculations
to one global `float3` with no precision-management mechanism.

This is an architecture guardrail, not a selected implementation. Milestone 2
does not have to implement origin rebasing or operational-area streaming.

## Geometry, draw and object budgets

Initial visible-geometry engineering budgets are:

```text
normal gameplay             2-3 million visible triangles
stress scenario             up to 5 million visible triangles
```

The 5 million stress budget is a profiling workload, not permission to render
that amount continuously without measurement.

Content geometry guidelines are:

```text
hero/player submarine LOD0                 50k-150k triangles
hero/player submarine LOD1                 approximately 50% of LOD0
hero/player submarine LOD2                 approximately 15-25% of LOD0
distant representation                    substantially cheaper
large surface ship / major vessel LOD0    50k-150k when close-view quality
                                            materially requires it
small props                                substantially cheaper
```

The LOD0 range is an upper content guideline, not a target to consume for every
vessel. Triangle count alone is not a sufficient performance metric. A content
or renderer review must consider visible triangles, draw calls, renderable count,
material count, state changes, VRAM, CPU submission cost and GPU frame time
together. Until representative materials exist, do not invent an unsupported
universal material-count limit; minimize unique materials and state changes and
derive their engineering budget from captures of representative content.

Until a proven GPU-driven submission path replaces the current indexed D3D12
renderer assumptions, use these draw-call engineering budgets:

```text
normal gameplay             < 1500 draw calls
stress scenario             < 3000 draw calls
```

Normal gameplay should be designed around hundreds to low-thousands of
simultaneously relevant renderables, not tens of thousands of individually
CPU-submitted objects. These are not permanent limits for a future renderer.
GPU culling, ExecuteIndirect, meshlets, Mesh Shaders or equivalent batching may
change the appropriate budget after representative profiling, while the classic
indexed compatibility path remains governed by measured CPU submission cost.

## Memory and texture budgets

The RX 6600 has 8 GB of VRAM, but the engine/game-owned steady-state target for
normal gameplay is a 5.5-6 GB VRAM working budget. The remainder is headroom for
the OS, graphics driver, compositor, transient GPU resources and differences
introduced by debugging or profiling tools.

Normal gameplay targets an 8-12 GB RAM working set. A machine with 16 GB of
system RAM must remain a realistic supported configuration; the 32 GB reference
PC is not permission to design a game that requires 32 GB.

Memory telemetry must measure runtime committed/resident use and working sets.
Asset file sizes alone are not evidence that RAM or VRAM budgets are satisfied.
Texture-resolution, mipmap and compression requirements are defined by the
canonical `Texture policy` in the rendering architecture above.

## Lights, shadows and particles

Expensive real-time shadow-casting lights are a bounded resource. Content must
not assume hundreds of simultaneously active dynamic shadow-casting lights.
Lighting limits should be selected from representative GPU captures rather than
an unsupported universal light-count constant.

Bubbles, cavitation, explosions, debris and underwater effects must have bounded
counts, distance culling and a LOD/degradation path that can reduce update and
render cost. Exact particle-count limits require representative profiling data;
until then, effects must still preserve the 60 FPS performance target and may not
grow without bounds.

## Physics and simulation tiers

Full-rate authoritative simulation is a bounded resource. Not every logical-world
entity needs a Jolt rigid body, a 60 Hz update or participation in every collision
query. Distance- and relevance-based simulation tiers are expected.

A distant contact may use simplified kinematics, low-frequency updates, an
analytic trajectory and acoustic/contact state instead of full rigid-body
simulation. The selected simulation tier changes cost and representation, not
the ownership of truth: authoritative simulation state remains independent of
presentation state.

## Performance telemetry

The metrics enumerated in `# 60. Diagnostics` are the canonical runtime
performance telemetry contract. They are also a roadmap guardrail: relevant
metrics must become available as the corresponding renderer or simulation work
is implemented, but Milestone 2 does not require every future counter.

## Performance regression rule

A renderer, simulation or content feature is not complete merely because it
looks correct; its cost must be measurable. Performance regressions must be
compared against a reproducible reference scenario and the reference PC contract,
with CPU and GPU timings captured separately.

A future automated benchmark should combine representative submarine geometry,
enemies, torpedoes, mines, particles, underwater fog, ocean presentation,
lighting and UI. Stress scenarios must also be reproducible. Reports must include
enough telemetry to compare changes rather than relying on average FPS or the
statement that a developer PC currently feels fast.

Optimization is profiling-driven. Avoid premature optimization, but do not make
architectural decisions that preclude future batching, culling, streaming, LOD or
simulation tiers.

## Milestone 2 guardrail

These contracts define architecture requirements, profiling targets and future
content budgets. They do not expand the canonical Milestone 2 scope. M2 is not
required to implement giant-world streaming, origin rebasing, GPU-driven
rendering, mesh shaders, meshlets, DirectStorage, advanced texture streaming,
Hi-Z occlusion or production acoustic simulation.

---
# 68. Simulation update rates

Начальные значения:

```text
Rigid Physics      60 Hz
Marine Physics     60 Hz
Flooding           30 Hz
Damage             event driven
Acoustics          10–20 Hz
Sonar UI           20 Hz
AI                  5–10 Hz
Crew Tasks         10 Hz
Roguelite           event driven
Rendering          variable
Audio              backend controlled
```

Не обновлять всё 60 раз в секунду без причины.

---

# 69. Data-driven definitions

Например:

```text
Content/Submarines/k314_grom.json
```

```json
{
  "id": "k314_grom",
  "displayName": "K-314 Grom",

  "mass": 12000,
  "maxHull": 100,

  "operationalDepth": 450,
  "criticalDepth": 650,

  "systems": {
    "reactor": 100,
    "sonar": 100,
    "weapons": 100,
    "propulsion": 100
  }
}
```

Значения — игровые.

Не использовать реальные секретные характеристики.

---

# 70. Encounter definition

```text
Content/Encounters/asw_patrol.json
```

```json
{
  "id": "asw_patrol",

  "environment": "north_atlantic",

  "player": {
    "depth": 180
  },

  "enemies": [
    {
      "type": "asw_destroyer",
      "position": [3200, 0, 0]
    }
  ]
}
```

---

# 71. Acoustic definition

```text
Content/Ships/asw_destroyer.acoustic.json
```

```json
{
  "idle": {
    "broadband": 0.35,
    "machinery": 0.45
  },

  "cruise": {
    "broadband": 0.65,
    "propeller": 0.70
  },

  "full": {
    "broadband": 0.90,
    "cavitation": 1.0
  }
}
```

---

# 72. Engine configuration

```text
Config/engine.json
```

Configuration loader обязан проверять типы и диапазоны значений, возвращать путь и полезное описание
ошибки и не зависеть от machine-specific absolute paths. Milestone 1 загружает только уже используемые
`renderer` и `physics` sections; gameplay balancing и acoustics config добавляются позже.

Пример:

```json
{
  "renderer": {
    "vsync": true,
    "width": 1920,
    "height": 1080
  },

  "physics": {
    "fixedHz": 60
  },

  "acoustics": {
    "updateHz": 15,
    "maxReflectionDepth": 2
  }
}
```

---

# 73. Logging

Категории:

```text
Core
Render
Physics
Marine
Audio
Acoustics
AI
Game
Assets
UI
```

Пример:

```text
[Acoustics][DEBUG] Destroyer #3 -> Player SNR: 14.2
```

---

# 74. Error policy

Использовать:

```text
assert
```

для programming errors.

Использовать normal error handling для:

```text
missing assets
invalid config
device loss
failed file access
```

Не падать из-за отсутствия декоративного audio asset.

---

# 75. Editor policy

НЕ создавать полноценный DeepRun Editor.

На первом этапе контент создаётся через:

```text
Visual Studio
JSON
Blender
Image editor
Audio editor
Dear ImGui tools
```

Если появится реальная необходимость, отдельный editor рассматривается позже.

---

# 76. Tools

Допустимые маленькие tools:

## AssetCooker

Конвертация production assets.

## SceneValidator

Проверяет JSON scenes и references.

## AcousticDebugger

Позволяет экспериментировать с acoustic simulation.

Но каждый tool создаётся только когда реально нужен.

---

# 77. Hard Non-Goals

DeepRun Engine НЕ должен сейчас поддерживать:

* Vulkan;
* OpenGL;
* Metal;
* Android;
* iOS;
* PlayStation;
* web;
* networking;
* multiplayer;
* Lua;
* Python scripting;
* C# scripting;
* visual scripting;
* generic editor;
* generic animation graph;
* arbitrary plugin ecosystem;
* general-purpose terrain;
* voxel engine;
* destruction engine;
* real-time CFD;
* Navier–Stokes ocean simulation;
* physically accurate acoustic wave solver;
* physically exact submarine simulator.

---

# 78. Что является физикой, а что gameplay

Очень важная граница.

### Physics

```text
position
velocity
mass
force
collision
pressure
flow
buoyancy
drag
```

### Gameplay simulation

```text
system efficiency
crew skill
detection confidence
damage probability
torpedo decision
AI behavior
mission success
```

Нельзя пытаться превратить gameplay в академическую physics simulation.

---

# 79. Engine Coding Rules

Использовать:

```text
RAII
smart pointers
span
string_view
enum class
strong types где полезно
constexpr
```

Избегать:

```text
global mutable state
singletons в gameplay
raw owning pointers
massive inheritance trees
macros вместо языка
```

---

# 80. Dependency direction

Нельзя:

```text
Engine → Game
```

Можно:

```text
Game → Engine
Game → Simulation
Simulation → Engine
```

---

# 81. Interfaces

Пример правильного observation-oriented API:

```cpp
class AcousticWorld
{
public:
    void Update(SimulationTime now);

    ObservationBatch CollectObservations(
        const AcousticReceiver& receiver) const;
};
```

Caller не передаёт target или hostile `EntityId`. `AcousticWorld` может
использовать entity identifiers внутри simulation и developer diagnostics, но
authoritative source identity не должна попадать обычному sensor consumer.
Contacts и Tracks создаются после observation stage.

Неправильно:

```cpp
DetectEnemySubmarine(EntityId knownEnemy);
```

Target-specific detection API обходит perception pipeline и запрещён.

---

# 82. Event System

Типичные events:

```text
CollisionEvent
DamageEvent
HullBreachEvent
SystemFailureEvent
PowerChangedEvent
SonarContactEvent
TorpedoLaunchEvent
CompartmentFloodedEvent
CrewInjuredEvent
```

Использовать events там, где действительно есть asynchronous reaction.

Не превращать каждую function call в EventBus message.

---

# 83. DeepRun Runtime States

```text
BOOT
↓
MAIN_MENU
↓
RUN_MAP
↓
ENCOUNTER
↓
RUN_MAP
↓
...
↓
RUN_SUMMARY
```

Encounter внутри имеет:

```text
LOADING
PLAYING
PAUSED
VICTORY
ESCAPED
DEFEAT
```

---

# 84. Rendering / Simulation separation

Renderer никогда не определяет gameplay state.

Например:

```text
CavitationSystem
        ↓
cavitation = 0.73
        ↓
 ┌──────────────┐
 │              │
Audio          Renderer
noise          bubbles
```

Particles не являются самой кавитацией.

Они только визуализируют её.

---

# 85. Damage / Visual separation

Аналогично:

```text
HullBreach
    ↓
FloodingSystem
    ↓
waterVolume
    ↓
Renderer
```

Визуальная вода в отсеке НЕ является источником истины.

---

# 86. Acoustics / Audio separation

```text
AcousticWorld
      ↓
acoustic observations
      ↓
Contact / Track processing
      ↓
UI / AI / Weapons
```

и отдельно:

```text
AcousticWorld
      ↓
audible parameters
      ↓
AudioEngine
      ↓
player headphones
```

Это один из ключевых архитектурных принципов всего проекта.

---

# 87. Первый executable

После самого первого рабочего milestone должны существовать:

```text
DeepRun.exe
DeepRunTests.exe
```

DeepRun.exe должен открыть окно и показать:

```text
DeepRun Engine
Renderer: D3D12
Physics: Jolt
Audio: miniaudio
```

---

# 88. Milestone 0 — Engine Bootstrap

Definition of Done:

```text
CMake работает
DeepRun.exe запускается
D3D12 device создаётся
окно работает
controller определяется
miniaudio инициализируется
Jolt инициализируется
ImGui работает
логирование работает
headless mode работает
```

---

# 89. Milestone 1 — Core Engine

Должно работать:

```text
explicit lifecycle
frame state
Scene and entity foundation
Transform and Tag
runtime asset identity and cache
validated JSON configuration
engine input state
headless core tests
```

Submarine gameplay и physical playground в этот milestone не входят.

---

# 90. Milestone 2 — Physical Playground

Должно работать:

```text
submarine mesh
submarine rigid body
basic water plane
buoyancy
hydrodynamic drag
propulsion / thrust
control surfaces
depth response
side-view camera
controller-driven submarine commands
basic gamepad haptics
```

Игрок уже может плавать.

---

# 91. Milestone 3 — Underwater Environment

Добавить:

```text
depth lighting
fog
particles
bounded scene-linear HDR / SDR output foundation
seabed and representative rocks / ridges / cliffs / drop-offs
basic underwater ice geometry
minimal presentation-first flora
optional cheap presentation fauna
surface
Gerstner waves
basic ship buoyancy
```

M3 remains a bounded environment/presentation milestone. It does not implement
combat, tactical AI, weapon runtime, crew, flooding, or system management.

---

# 92. Milestone 4 — Acoustic Playground

M4 is preceded by IG1 -- Production Antey Runtime Integration. IG1 supplies the
production runtime representation; it does not move asset migration or content
authoring cleanup into the acoustic milestone.

Добавить:

```text
AcousticWorld with coarse spectral emitters and receivers
bounded propagation, delay, environmental loss and noise
passive and active AcousticObservations with SNR and uncertainty
minimal Contact / Track vertical slice
cavitation acoustic signature
gameplay-relevant biological acoustic source where useful
Acoustic Debugger
```

---

# 93. Milestone 5 — Combat Playground

Добавить:

```text
destroyer
first conventional heavyweight torpedo
decoy
mine
explosion
damage
simple combat AI using perceived contacts / tracks
```

---

# 94. Milestone 6 — Submarine Systems

Добавить:

```text
compartments
flooding
pumps
reactor
power distribution
lights
system damage
crew
repair
watertight / fire boundaries and authoritative access graph
fire / smoke / heat and O2 / CO2 habitability
crew isolation, qualifications, protection and team assignment
proper / degraded / containment repair outcomes
```

---

# 95. Milestone 7 — Cascading Failure Scenario

Первый главный gameplay test:

```text
enemy detects submarine
↓
torpedo launched
↓
submarine hit
↓
compartment breached
↓
flooding
↓
power failure
↓
crew reassignment
↓
pump activation
↓
escape
```

Если этот сценарий интересен — фундамент игры работает.

---

# 96. Milestone 8 — Roguelite

Только теперь добавить:

```text
run
seed
map
encounters
events
rewards
upgrades
death
new run
```

---

# 97. Milestone 9 — Advanced Warfare

После доказанного core gameplay:

```text
helicopters
aircraft
sonobuoys
P700 runtime using the accepted C0 asset
supercavitating / Shkval-inspired weapon
smaller / lighter torpedo family
advanced mines and dropped ASW weapons
underwater launch
surface transition
AUG
advanced enemy submarine and tactical group AI
```

---

# 98. Критерий успеха движка

DeepRun Engine считается удачным не тогда, когда:

> у него много функций.

А когда конкретный сценарий:

```text
движение
→ акустический поиск
→ обнаружение
→ торпедный бой
→ повреждение
→ затопление
→ борьба за живучесть
→ спасение
```

может быть реализован:

* предсказуемо;
* без hacks;
* с хорошим debug tooling;
* при стабильных 60 FPS;
* без зависимости gameplay от Windows-specific API;
* с возможностью впоследствии перенести runtime на Xbox.

---

# 99. Главный архитектурный принцип

Мы пишем не:

> Custom Engine + Game

как два независимых продукта.

Мы пишем:

> **DeepRun Game + минимально необходимый DeepRun Engine вокруг неё.**

Если новая engine feature не нужна конкретной механике Deep Run, она не должна появляться в roadmap.

---

# 100. Итоговая технологическая схема

```text
                         DEEP RUN
                            │
       ┌────────────────────┼─────────────────────┐
       │                    │                     │
      GAME              SIMULATION             UI
       │                    │
       │        ┌───────────┼────────────┐
       │        │           │            │
      Crew     Marine     Damage      Acoustics
       │        │           │            │
       └────────┴───────────┴────────────┘
                            │
                    DEEPRUN ENGINE
                            │
       ┌─────────┬──────────┼──────────┬────────┐
       │         │          │          │        │
     D3D12      Jolt     miniaudio    Input   Assets
       │         │          │          │        │
       └─────────┴──────────┴──────────┴────────┘
                            │
                         PLATFORM
                       ┌────┴────┐
                    Windows    Xbox
```

---

# 101. Самое важное правило для Codex

При реализации любой подсистемы Codex обязан сначала ответить на вопрос:

> **Какая конкретная gameplay-механика Deep Run требует эту систему прямо сейчас?**

Если убедительного ответа нет — систему пока не создавать.

При выборе между:

* универсальностью;
* академической точностью;
* сложной архитектурой;
* работающей игровой механикой;

приоритет:

```text
1. работающий gameplay
2. корректная архитектурная граница
3. удобная отладка
4. производительность
5. физическая правдоподобность
6. универсальность
```

DeepRun Engine должен быть специализированным игровым движком, а не попыткой создать новый Unreal Engine.

<!-- BEGIN: acoustic-detection-engine-contract -->

# 102. Acoustic, Detection and Signature Simulation

## Canonical specifications

Detailed acoustic and sonar behaviour is defined in:

`docs/architecture/acoustics-spec.md`

Persistent signatures, wake simulation, contacts, tracks and perception
boundaries are defined in:

`docs/architecture/simulation-spec.md`

This section defines only engine-level ownership and integration boundaries.

## Required simulation services

The architecture SHALL support services conceptually equivalent to:

- `AcousticWorld`;
- `SignatureFieldWorld`;
- `SensorWorld`;
- `TrackManager`.

Exact class names and file layout may evolve.

Their responsibilities SHALL remain separated from presentation systems.

## Ownership boundaries

Engine owns generic platform, timing, physics-query and presentation services;
it does not own gameplay acoustic or perceived-state knowledge.

`Simulation/Acoustics` owns acoustic signatures, propagation and
`AcousticObservation` production.

The Simulation perceived-state layer owns sensor observations, contacts,
tracks and persistent signature fields. Exact Contact and Track file placement
remains an implementation detail.

Game UI, AI and weapons consume perceived-state information appropriate to
their role. They do not bypass Simulation to obtain hostile ground truth.

## Perception pipeline

The engine architecture SHALL preserve this information flow:

Ground truth
-> signatures
-> propagation/environment
-> sensor observations
-> contacts
-> tracks
-> gameplay consumers.

Normal UI, AI and weapon systems MUST NOT receive perfect hostile entity state
when only imperfect sensor knowledge is available.

## Jolt boundary

Jolt remains authoritative for:

- rigid-body simulation;
- physical collisions;
- collision geometry;
- ray and shape queries.

Simulation systems MAY query Jolt for coarse environmental information.

Jolt does NOT own:

- acoustic propagation;
- sonar;
- acoustic reflections;
- reverberation;
- contacts;
- target tracks;
- hydrodynamic wake simulation.

Physical events MAY generate acoustic or persistent-signature events.

## miniaudio boundary

miniaudio remains authoritative for runtime audio playback.

It does NOT determine gameplay:

- propagation;
- detection;
- contact state;
- classification;
- AI knowledge;
- track state.

Gameplay acoustics MUST remain functional even when audio output is disabled
or unavailable.

## Presentation boundary

The renderer and `AudioEngine` visualize or present simulation-owned state.
They do not create acoustic observations, contacts, tracks or persistent
signatures. Ground-truth overlays are restricted to developer diagnostics.

## Deterministic simulation time

Acoustic events, propagation arrivals, wake ageing, sensor observations and
track updates SHALL use simulation-owned time.

Presentation timing MUST NOT be authoritative.

## Symmetric knowledge

Player and AI vessels SHALL participate in the same signature, observation
and track architecture.

Differences in capability SHOULD arise through authored:

- sensor quality;
- platform characteristics;
- environmental state;
- crew/AI skill;
- difficulty tuning.

They SHOULD NOT arise from hidden omniscient access to hostile ground truth.

## Performance boundary

The engine explicitly permits bounded approximations such as:

- spatial partitioning;
- coarse frequency bands;
- scheduled acoustic events;
- bounded propagation/reflection paths;
- reverberation envelopes;
- sparse wake trails;
- fixed-rate sensor integration;
- distance-based update reduction.

The following are explicitly out of scope:

- full ocean CFD;
- physical particles representing each sound wave;
- full numerical wave-equation simulation;
- per-audio-sample gameplay acoustics.

<!-- END: acoustic-detection-engine-contract -->
