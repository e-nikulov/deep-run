# DeepRun Controls and Haptics

Status: Accepted baseline

Specification: D1 — Controls & Handling

The project-wide M/A/D/C taxonomy and specification registry are defined in
`docs/README.md`. D1 is a design contract, not a milestone or an
implementation-sequence position.

This document defines the reference player-facing control scheme and
controller haptic language for DeepRun.

Engine-level input architecture is defined by
`docs/architecture/engine-spec.md`.

Exact physical bindings may be tuned during playtesting. Gameplay code
must depend only on semantic actions and axes.

---

## 1. Control philosophy

Xbox-compatible controller is the reference control scheme.

The complete production game must be playable using only a controller.

Keyboard and mouse are a feature-equivalent alternative on PC.

Gameplay code consumes semantic actions and normalized axes rather than
physical keyboard keys, mouse buttons, or XInput constants.

Canonical gameplay inputs:

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

Physical bindings belong to input configuration, not submarine simulation.

---

## 2. Submarine control semantics

DeepRun must not control the submarine like a weightless arcade character.

The player commands the vessel. The simulation determines how the vessel
responds.

### Xbox controller

Left Stick X:

```text
astern <- stop -> ahead
```

This maps to the semantic `Throttle` axis.

It represents propulsion command rather than direct position or velocity.

Left Stick Y:

```text
surface command <- neutral -> dive command
```

This maps to the semantic `Depth` control axis.

It represents requested vertical/depth manoeuvre rather than directly
changing submarine position.

The implemented I1 signs are explicit:

```text
Throttle: -1 astern, +1 ahead
Depth:    -1 surface / nose-up, +1 dive / nose-down

Left Stick up / W = Depth -1 (surface)
Left Stick down / S = Depth +1 (dive)
```

Actual vessel motion is determined by:

```text
propulsion
inertia
buoyancy
hydrodynamic drag
control surfaces
damage state
```

### Keyboard

```text
A / D    Throttle
W / S    Depth
```

Digital keyboard input must be converted to the same semantic gameplay
controls used by the controller.

---

## 3. Reference Xbox controller layout

| Control | Default action |
|---|---|
| Left Stick X | throttle / ahead-astern command |
| Left Stick Y | depth / vertical manoeuvre command |
| Right Stick | tactical cursor / aiming |
| A | interact / confirm |
| B | cancel / back |
| X | contextual submarine-system action |
| Y | select / cycle contact or context |
| RT | fire selected weapon |
| LT | prepare weapon / targeting |
| RB | active sonar ping |
| LB | silent-running / acoustic-control action |
| D-pad | quick submarine/system commands |
| View | tactical view / tactical map |
| Menu | pause / system menu |

Context-specific actions may vary by active station or UI screen.

Global actions should remain consistent whenever practical.

---

## 4. Reference keyboard and mouse layout

| Control | Default action |
|---|---|
| A / D | throttle / ahead-astern command |
| W / S | depth / vertical manoeuvre |
| Mouse | tactical cursor / aiming |
| Left Mouse | fire / confirm according to context |
| Right Mouse | prepare weapon / alternate according to context |
| E | interact |
| F | contextual submarine-system action |
| Space | active sonar ping |
| Left Shift | silent-running / acoustic-control action |
| Tab | select / cycle contact |
| 1-4 | quick submarine/system commands |
| M | tactical view / tactical map |
| Esc | cancel / pause |
| F1 | developer/debug UI when enabled |

Keyboard bindings must eventually be rebindable.

---

## 5. Controller UI navigation

No production gameplay screen may require mouse input.

Baseline navigation:

```text
Left Stick / D-pad -> move focus / navigate
A                  -> activate / confirm
B                  -> cancel / back
LB / RB            -> switch tabs or stations
View               -> tactical overview where applicable
Menu               -> pause
```

Crew, compartment, damage-control, sonar and submarine-system
interfaces must expose predictable controller focus navigation.

Mouse interaction on PC is an additional fast interaction method, not a
required gameplay capability.

---

## 6. Analog input

The engine normalizes physical device values before they reach gameplay.

Expected ranges:

```text
stick axis: -1.0 .. +1.0
trigger:     0.0 .. 1.0
```

The Input layer owns:

```text
dead zones
normalization
response curves
sensitivity
axis inversion
device connection state
```

Submarine simulation must not contain XInput-specific dead-zone or raw
device handling logic.

---

## 7. Haptics philosophy

Controller vibration is both presentation feedback and a secondary
gameplay-information channel.

It should reinforce:

```text
submarine mass
propulsion
machinery
cavitation
acoustics
weapon launch
impacts
explosions
damage
threat proximity
```

Haptics must never be the only way essential gameplay information is
communicated.

Vibration must be optional and have configurable master intensity.

---

## 8. Semantic haptic events

Baseline events:

```text
EngineVibration
Cavitation
ActiveSonarPing
IncomingSonarPing
TorpedoLaunch
ExplosionNear
ExplosionFar
HullImpact
GroundContact
Damage
DamageCritical
FloodingPulse
IncomingTorpedo
```

Gameplay emits semantic events.

Gameplay must not directly set controller motor values.

---

## 9. Reference vibration language

| Event | Feedback |
|---|---|
| low propulsion | extremely subtle low-frequency vibration |
| high propulsion | stronger continuous low-frequency vibration |
| cavitation | fine high-frequency vibration |
| own active sonar ping | short clean pulse |
| incoming sonar ping | distinct short pulse |
| torpedo launch | short mechanical impulse |
| distant explosion | weak low-frequency impulse |
| nearby explosion | strong dual-motor impulse |
| hull impact | sharp impulse |
| ground contact | heavy irregular vibration |
| damaged machinery | periodic mechanical vibration |
| incoming torpedo | escalating pulse pattern |

Exact strengths and durations are tuning data and must be refined during
playtesting.

---

## 10. Two-motor convention

When the backend exposes two motors:

```text
low-frequency motor
    hull
    propulsion
    explosions
    heavy impacts

high-frequency motor
    cavitation
    machinery
    damage texture
    acoustic / technical feedback
```

This is a reference convention rather than a restriction on effect mixing.

---

## 11. Haptic architecture

Expected data flow:

```text
Gameplay
    |
    v
HapticEvent
    |
    v
HapticSystem
    |
    v
Gamepad output backend
    |
    +-- Windows desktop: XInput
    |
    `-- Xbox: platform / GDK backend
```

Engine-facing motor state may use normalized values:

```cpp
struct GamepadVibration
{
    float lowFrequency = 0.0F;
    float highFrequency = 0.0F;
};
```

Range:

```text
0.0 .. 1.0
```

`HapticSystem` owns:

```text
effect duration
effect priority
mixing
clamping
master intensity
global enable / disable
effect cancellation
device disconnect handling
```

---

## 12. Simulation boundary

Haptic output is presentation state.

It must never:

```text
modify submarine simulation
modify physics
modify AI
modify acoustic simulation
modify deterministic random state
modify save-game state
```

Headless execution must work normally without gamepad or haptic hardware.

---

## 13. Platform boundary

The Windows desktop backend may use:

```cpp
XInputGetState(...)
XInputSetState(...)
```

These calls belong only to the platform/input implementation.

They must not appear in gameplay or simulation code.

Future Xbox/GDK support must be implementable using another backend without
changing gameplay actions or semantic haptic events.

---

## 14. Accessibility and configuration

Player settings must eventually expose:

```text
controller vibration enabled
controller vibration intensity
stick dead zones
stick sensitivity
stick inversion where applicable
keyboard rebinding
controller rebinding where platform rules allow
```

Information represented through haptics must also have visual and/or audio
feedback.

---

## 15. Design rule

DeepRun is controller-first, not controller-only.

Xbox-compatible controller defines the reference interaction model.

Keyboard and mouse provide feature-equivalent control on PC.
<!-- deeprun-command-controls:start -->

---

## 16. Direct control and command layer

DeepRun has two interaction modes:

```text
direct vessel commands
commander / systems commands
```

Direct vessel commands use the existing semantic controls such as:

```text
Throttle
Depth
AimX
AimY
```

Future systems commands are discrete actions initiated through controller-first
shipping UI.

Examples:

```text
change power priority
change sonar mode
prepare weapon
seal compartment boundary
activate pump
assign damage-control team
```

The UI issues gameplay commands. It does not directly write authoritative
simulation state.

---

## 17. Tactical pause

Future combat may introduce a semantic `TacticalPause` action.

This must remain distinct from the existing general `Pause` / system-menu
concept.

`TacticalPause` is not required for Milestone 2 and no M2 binding must be
changed merely to reserve a button for it.

When implemented, tactical pause must:

```text
freeze gameplay simulation
keep UI navigation active
allow inspection of current known information
allow only explicitly supported queued commands
resume deterministically
```

Exact controller and keyboard bindings should be selected during the Milestone
5 combat control pass.

---

## 18. Crew interaction

Authoritative crew simulation may track important crew members individually,
including specialty, qualification, availability, health, current compartment,
protective-equipment state, and assignment.

Player interaction remains primarily team / role / watch oriented.

The production control model must not require moving every individual crew
member as an FTL-style room sprite.

Controller interactions should operate on meaningful orders such as:

```text
assign best available team
assign damage-control team
seal damaged compartment
attempt emergency access
rescue isolated crew
route via alternate path
prioritize repair
inspect required qualifications
inspect proposed specialists
inspect blocked route / boundary
override a proposed assignment
restore system
cancel assignment
```

Individual selection is an optional precision tool for exceptional situations,
not the default interaction loop.

The UI must clearly distinguish:

```text
qualified but unavailable
qualified but unreachable
reachable but unqualified
available with required protection
isolated / trapped
```

## 19. Command-layer navigation

Future systems screens must preserve the existing controller-first contract.

Recommended navigation vocabulary:

```text
D-pad / Left Stick -> focus
A                  -> confirm / issue
B                  -> back / cancel
LB / RB            -> stations / tabs
View               -> tactical overview where applicable
```

Do not hard-code a final station layout before the corresponding gameplay
milestone exists.

Detailed command-layer behavior is defined in:

```text
docs/design/submarine-command.md
docs/design/game-loop.md
```

<!-- deeprun-command-controls:end -->
