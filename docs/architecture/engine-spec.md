# DeepRun Engine — Technical Specification v0.1

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
Visual Studio 2022
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

# 5. Структура repository

```text
deep-run/
│
├── CMakeLists.txt
├── CMakePresets.json
├── README.md
├── LICENSE
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

---

# 18. Buoyancy

Объект может иметь несколько buoyancy points:

```text
bow
stern
port
starboard
center
```

Для каждой точки:

```text
water level
submersion
buoyant force
```

Получаем:

```text
pitch
roll
heave
```

Без CFD.

---

# 19. Hydrodynamic Drag

Drag должен учитывать направление.

Например:

```text
longitudinal drag
lateral drag
vertical drag
angular drag
```

Это позволит лодке ощущаться:

* тяжёлой;
* инерционной;
* устойчивой вдоль корпуса;
* плохо двигающейся боком.

---

# 20. Propulsion

```cpp
struct PropulsionComponent
{
    float requestedPower;
    float availablePower;
    float rpm;
    float thrust;
    float efficiency;
};
```

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

Это НЕ Audio Engine.

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
├── SonarContact.h
└── AcousticDebugger.h
```

---

# 34. Acoustic Signature

Каждый шумящий объект имеет:

```text
broadband noise
propeller noise
engine noise
machinery noise
cavitation noise
transient noise
```

Можно представить спектр несколькими frequency bands.

Например:

```text
31 Hz
63 Hz
125 Hz
250 Hz
500 Hz
1 kHz
2 kHz
4 kHz
```

Не требуется полноценный waveform simulation.

---

# 35. Passive Sonar

Расчёт:

```text
Source
 ↓
Transmission Loss
 ↓
Thermocline
 ↓
Terrain Occlusion
 ↓
Ambient Noise
 ↓
Receiver
```

Основной gameplay результат:

```text
SNR
```

На основании SNR определяются:

```text
Detection
Classification
BearingAccuracy
TrackingQuality
```

---

# 36. Active Sonar

Active ping:

```text
Emitter
 ↓
Propagation
 ↓
Object
 ↓
Reflection
 ↓
Return propagation
 ↓
Receiver
```

Результат:

```text
range
bearing
contact confidence
classification quality
```

Использовать acoustic ray approximation.

Не моделировать реальные волновые уравнения воды.

---

# 37. Sonar Environment

Система должна учитывать:

```text
depth
thermocline
seabed material
surface state
water column
terrain
ambient noise
shipping noise
weather
```

---

# 38. Acoustic Debugger

Очень важный инструмент.

Должен показывать:

```text
source
receiver
rays
reflections
SNR
losses
thermocline
contact confidence
```

Например:

```text
Source Level       82
Distance Loss     -28
Thermocline       -12
Terrain            -8
Ambient Noise     -19
---------------------
Final SNR          15
```

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

Sonar seeker использует AcousticWorld.

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

Использовать:

```text
Hierarchical State Machines
+
Blackboard
+
Perception
+
Tactical Director
```

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
frame time
physics time
acoustics time
AI time
draw calls
triangles
GPU memory
entities
audio voices
physics bodies
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

# 67. Performance budgets

Цель:

```text
1920×1080
60 FPS
```

Основной будущий baseline:

```text
Xbox Series S
```

CPU target:

```text
simulation < 8 ms
```

GPU target:

```text
rendering < 16 ms
```

Желательно:

```text
rendering < 12 ms
```

для запаса.

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

Пример правильного API:

```cpp
class AcousticWorld
{
public:
    AcousticContact QueryPassive(
        const AcousticReceiver& receiver,
        EntityId emitter) const;
};
```

Неправильно:

```cpp
CalculateEnemyDestroyerSonarDetectionForGrom();
```

Simulation должна оставаться переиспользуемой внутри игры.

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
simulation result
      ↓
Sonar gameplay
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

# 89. Milestone 1 — Physical Playground

Должно работать:

```text
ocean
submarine mesh
camera
rigid body
buoyancy
thrust
depth
drag
controller
```

Игрок уже может плавать.

---

# 90. Milestone 2 — Underwater Environment

Добавить:

```text
depth lighting
fog
particles
surface
Gerstner waves
basic ship buoyancy
```

---

# 91. Milestone 3 — Acoustic Playground

Добавить:

```text
acoustic emitters
passive sonar
active sonar
SNR
distance loss
terrain occlusion
basic thermocline
cavitation noise
Acoustic Debugger
```

---

# 92. Milestone 4 — Combat Playground

Добавить:

```text
destroyer
torpedo
decoy
mine
explosion
damage
```

---

# 93. Milestone 5 — Submarine Systems

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
```

---

# 94. Milestone 6 — Cascading Failure Scenario

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

# 95. Milestone 7 — Roguelite

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

# 96. Milestone 8 — Advanced Warfare

После доказанного core gameplay:

```text
helicopters
aircraft
sonobuoys
missiles
underwater launch
surface transition
AUG
group AI
```

---

# 97. Критерий успеха движка

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

# 98. Главный архитектурный принцип

Мы пишем не:

> Custom Engine + Game

как два независимых продукта.

Мы пишем:

> **DeepRun Game + минимально необходимый DeepRun Engine вокруг неё.**

Если новая engine feature не нужна конкретной механике Deep Run, она не должна появляться в roadmap.

---

# 99. Итоговая технологическая схема

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

# 100. Самое важное правило для Codex

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
