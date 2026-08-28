# DeepRun Submarine Command Layer

Status: Accepted design direction

This document defines the player-facing submarine command model for DeepRun.

It is a gameplay/design contract. It does not expand Milestone 1 or Milestone 2 scope.

Authoritative engine/input boundaries remain in:

```text
docs/architecture/engine-spec.md
docs/design/controls.md
docs/roadmap/milestones.md
```

---

## 1. Core fantasy

DeepRun is not a cockpit-button simulator and not an FTL clone.

The player fantasy is:

> Command a submarine under uncertainty, where every useful action has a cost in time, power, noise, readiness, crew attention, or survivability.

The player should primarily make commander-level decisions:

```text
where to go
how fast to go
how deep to go
what to listen to
when to reveal the submarine
what system receives priority
whether to attack
whether to disengage
how to contain damage
whether to continue the mission or return
```

The game may borrow systemic pressure and resource trade-offs from FTL, but its systems must remain submarine-specific.

---

## 2. Two interaction layers

DeepRun has two complementary interaction layers.

### Direct vessel control

The player gives continuous or immediate vessel commands:

```text
Throttle
Depth
basic manoeuvre
target / tactical cursor
context actions
```

The simulation determines the physical response through:

```text
propulsion
inertia
buoyancy
hydrodynamic drag
control surfaces
damage state
```

This is the main Milestone 2 interaction model.

### Command layer

The player issues higher-level discrete orders:

```text
change power priority
seal / open a compartment boundary
activate a pump
assign a damage-control team
change sonar mode
prepare a weapon
select a tracked contact
queue an order during tactical pause
```

The command layer is introduced only when a gameplay milestone requires it.
It must not be implemented as unused generic engine infrastructure.

---

## 3. Command architecture

Production UI must never mutate authoritative submarine state directly.

Required dependency direction:

```text
Input / UI
    |
    v
semantic gameplay command
    |
    v
Game-owned command handling
    |
    v
authoritative submarine state
    |
    +--> Simulation systems
    |
    +--> observations / presentation models
    |
    v
UI / Audio / Haptics / Rendering
```

Examples of future gameplay commands:

```text
SetThrottle
SetDesiredDepth
SetPowerPriority
SetSonarMode
SelectContact
PrepareTube
LaunchWeapon
SealBoundary
OpenBoundary
StartPump
StopPump
AssignDamageControlTeam
CancelDamageControlOrder
```

Exact C++ types are deferred until their milestone.

Do not create a generic `Engine::CommandBus` merely because this document uses the word "command".
A game-specific command queue should appear only when Milestone 5 requires tactical pause / queued orders.

---

## 4. Authoritative submarine state

The authoritative submarine model is independent of UI.

It must be possible to run the relevant simulation headlessly.

Conceptually the submarine contains:

```text
Vessel state
├── motion / depth / trim
├── acoustic state
├── compartments
├── flooding
├── electrical / power state
├── propulsion state
├── sensors / sonar state
├── weapons readiness
├── damage
└── crew teams / assignments
```

Presentation may derive simplified views from this state, but presentation must never become the source of truth.

---

## 5. Power as a constrained resource

Power allocation is a commander-level trade-off, not a decorative stat.

Typical consumers may include:

```text
propulsion
sonar
pumps
life support
weapon preparation
auxiliary systems
reserve
```

The exact electrical model may remain abstracted.

The gameplay requirement is that the player cannot keep every high-demand system at maximum effectiveness simultaneously.

Power changes should have consequences such as:

```text
more propulsion
    -> more speed
    -> usually more self-noise / cavitation risk

more sonar processing
    -> better sensing capability
    -> less power elsewhere

more pumping
    -> slower flooding recovery
    -> power consumption
    -> possible acoustic cost

weapon preparation
    -> readiness improves
    -> power / crew / time cost
```

Power allocation must not behave like instant magic buffs.
Important systems should have bounded transition times where this improves gameplay.

---

## 6. Acoustic cost is the submarine equivalent of defense pressure

DeepRun should not use a conventional regenerating shield as the primary defense model.

Survivability depends heavily on not being found or not being accurately tracked.

The player's acoustic footprint is affected by systems including:

```text
speed
propulsion state
cavitation
pumps
damaged machinery
active sonar transmission
weapon launch
emergency actions
```

The design rule is:

> Useful emergency actions may save the submarine physically while making it easier to detect acoustically.

This coupling is central to DeepRun.

Examples:

```text
Flooding increases
    -> activate pump
    -> flooding rate improves
    -> noise increases

Incoming threat
    -> increase speed
    -> manoeuvrability improves
    -> self-noise rises
    -> passive sonar performance may fall
    -> enemy detection probability may rise
```

Noise values shown to the player may be abstracted and must not imply unrealistic precision.

---

## 7. Compartments and watertight boundaries

The submarine is divided into gameplay compartments.

A compartment may expose state such as:

```text
integrity
flooding
fire / smoke where applicable
power availability
important installed systems
crew presence
repair status
```

Watertight boundaries create meaningful decisions.

Example:

```text
breach
    -> flooding increases
    -> boundary can be sealed
    -> spread is limited
    -> crew access may be cut
    -> damaged equipment may become unavailable
```

The game must avoid presenting every internal door as a click-heavy simulation.
Only boundaries that create meaningful gameplay decisions should be modeled.

---

## 8. Flooding and buoyancy coupling

Flooding is not merely "submarine HP".

Water mass must influence the physical vessel state at the level required by gameplay.

Possible consequences:

```text
increased mass
trim change
loss of reserve buoyancy
increased depth tendency
reduced manoeuvrability
system failures
crew hazards
```

Damage control can counter these effects through:

```text
isolation
pumping
ballast actions
propulsion / depth control
repair
```

Emergency actions should have costs in time, noise, power, crew availability, or future capability.

---

## 9. Crew model

DeepRun should not require FTL-style movement of every individual sailor.

The default abstraction is teams, roles, or watches.

Possible groups:

```text
control room / command watch
sonar team
engineering watch
weapons team
damage-control team
```

Gameplay orders operate at team level:

```text
assign damage-control team to compartment
prioritize sonar station
support weapon preparation
repair pump
restore electrical bus
```

Crew assignments may affect:

```text
repair speed
system efficiency
fatigue / readiness later
availability elsewhere
casualty consequences
```

Individual named characters may exist for narrative or progression later, but the core interaction must not depend on moving dozens of sprites between rooms.

---

## 10. Sonar and uncertainty

Sonar is not a wall-hack.

The player should progressively build knowledge.

A contact may evolve conceptually:

```text
unknown bearing
    -> probable category
    -> stronger classification
    -> estimated range / motion
    -> firing-quality solution
```

The game can expose:

```text
bearing
confidence
classification confidence
range estimate / uncertainty
track quality
age of last observation
```

The authoritative truth of an enemy object's exact position must remain distinct from what the player knows.

The player-facing tactical layer consumes observations / tracks, not omniscient world state.

---

## 11. Active sonar

Active sonar is a high-information, high-exposure action.

It may provide faster or better range information while making the transmitter easier to detect.

The design goal is not "active sonar = bad".
The goal is:

> Active sonar is powerful enough that using it can be correct, but costly enough that using it is a decision.

---

## 12. Weapons

Weapons are prepared and launched through states rather than being instant arcade projectiles.

A future weapon workflow may include:

```text
select contact
obtain sufficient track quality
select weapon / tube
prepare
open / ready launch path where applicable
launch
weapon runs independently
```

Preparation and launch can consume:

```text
time
crew attention
power
readiness
acoustic discretion
limited ammunition
```

Milestone 5 should prove only the minimum state machine needed for enjoyable combat.

Do not build a full naval fire-control simulator before the core combat loop is fun.

---

## 13. Tactical pause

DeepRun may use real-time-with-pause for commander-level decisions.

Tactical pause is different from a settings / system menu pause.

When tactical pause is active:

```text
authoritative gameplay simulation is frozen
input navigation continues
production UI continues
audio may transition to an appropriate paused presentation state
the player may inspect state
the player may issue allowed queued commands
```

Commands queued during tactical pause are applied deterministically when simulation resumes, ideally on fixed-step boundaries.

Tactical pause must not:

```text
advance flooding
advance physics
advance weapon flight
advance AI
advance acoustics
advance deterministic gameplay timers
```

This feature is not required for Milestone 2.
It belongs to the first milestone where combat command sequencing benefits from it.

---

## 14. Command queue

The queue exists to support tactical pause and deliberate multi-system decisions.

The first implementation should remain intentionally small.

Potential properties:

```text
command type
target
parameters
issue order
validation result
execution state
```

Commands should be validated against current authoritative state.

Examples of rejection:

```text
pump is destroyed
weapon is not ready
boundary is already sealed
team is unavailable
target track is insufficient
```

The queue must not become a general-purpose scripting system.

---

## 15. System coupling matrix

The game becomes interesting through cross-system consequences.

| Action / state | Motion | Power | Noise | Sonar | Damage control | Weapons |
|---|---:|---:|---:|---:|---:|---:|
| increase speed | strong | medium | strong | may reduce own passive quality | none | positioning benefit |
| active sonar | none | low/medium | very strong exposure | strong information gain | none | improves targeting knowledge |
| run pumps | none | medium | low/medium | possible masking | strong benefit | none |
| flooding | strong at high level | may disable | indirect | may disable equipment | primary concern | may disable tubes |
| damaged machinery | possible | possible loss | increased | possible degradation | requires repair | possible degradation |
| weapon launch | small | small/medium | strong transient | contact consequences | none | primary effect |

Numbers are balancing data, not architectural constants.

---

## 16. UI principles

The command UI must answer three questions quickly:

```text
What is happening?
What can I do?
What will this choice cost?
```

Preferred views may include:

```text
external / navigation view
tactical contact view
submarine systems view
damage-control / compartment view
```

The player should not need to read dense engineering spreadsheets during immediate danger.

Information hierarchy should prioritize:

```text
critical threat
depth / motion
track quality
noise / exposure
flooding / integrity
power shortage
weapon readiness
crew task status
```

Controller-first focus navigation remains mandatory.

---

## 17. Failure philosophy

Damage should create problems, not merely subtract hit points.

Preferred chain:

```text
hit
    -> local damage
    -> breach / equipment failure
    -> flooding / power consequence
    -> sensor / propulsion / weapon consequence
    -> player response
    -> secondary trade-off
```

A hit can therefore change the player's plan instead of only shortening a health bar.

---

## 18. Retreat is a valid decision

The player is not expected to destroy every contact.

Valid commander decisions include:

```text
avoid
observe
shadow
attack
break contact
abort mission
return damaged
```

A run should support survival and mission judgement as meaningful outcomes.

---

## 19. Anti-goals

Do not turn DeepRun into:

```text
a room-by-room crew clicker
a full submarine training simulator
a generic power-grid simulator
a spreadsheet-first UI
a constant stream of combat encounters
an omniscient tactical map
an arcade shooter with decorative submarine systems
```

The systems exist to create decisions.

---

## 20. Milestone ownership

### M1

No changes.

Core engine only.

### M2 — Physical Playground

Prove:

```text
the submarine exists physically
the player can command movement
depth response is readable
controller control feels good
haptics support vessel feel
```

No compartments, power allocation, crew, tactical pause, sonar combat, or command queue.

### M3 — Underwater Environment

Presentation/environment milestone.

Do not introduce submarine-management systems merely to populate the environment.

### M4 — Acoustic Playground

Prove:

```text
the world emits sound
the submarine observes rather than knows
contacts / tracks carry uncertainty
passive and active sonar produce meaningful tactical information
```

A minimal read-only tactical presentation is allowed as needed to debug and play the acoustic slice.

### M5 — Combat Playground

Introduce the minimum commander command sequencing required for combat:

```text
weapon readiness
target / track dependency
torpedo / decoy interaction
basic damage
tactical pause if needed
minimal queued gameplay commands if needed
```

### M6 — Submarine Systems

Introduce the systemic command layer:

```text
compartments
flooding
pumps
power allocation
system damage
crew teams
repair
noise consequences
systems UI
```

### M7 — Cascading Failure Scenario

Prove that the systems create a compelling emergency chain.

### M8 — Roguelite Layer

Wrap the proven tactical/systemic game in:

```text
route decisions
encounters
events
rewards
upgrades
repair / continuation decisions
run summary
```

### M9 — Advanced Warfare

Expand the already proven loop with advanced threats and combined-force systems.

---

## 21. Success test

The command layer succeeds when a player can face a situation such as:

```text
contact detected
    -> classify under uncertainty
    -> choose whether to reveal the submarine
    -> prepare attack
    -> enemy reacts
    -> submarine is hit
    -> flooding starts
    -> power becomes insufficient
    -> pump activation increases noise
    -> crew is reassigned
    -> player chooses between continuing the fight and escaping
```

and every step follows from understandable state and player decisions rather than scripted exceptions.
