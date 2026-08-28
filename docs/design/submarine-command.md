# DeepRun Submarine Command Layer

Status: Accepted design direction

Specification: D0 — Core Game Design / Game Loop (command-layer contract)

The project-wide M/A/D/C taxonomy and specification registry are defined in
`docs/README.md`. This document specializes D0; it does not introduce another
sequence or reserve a new D-ID.

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

## 5. Operational capacity as a constrained resource

DeepRun must not use reactor power as its universal FTL-like resource.

For a nuclear submarine, the more interesting gameplay constraint is the
submarine's ability to handle several important tasks at the same time.

The primary constrained resources are:

```text
crew availability
crew qualifications
crew attention
time
system readiness
acoustic discretion
damage state
physical access
local electrical capacity
limited consumables / ammunition where applicable
```

Electrical power remains simulated at gameplay-relevant fidelity, especially
when generators, buses, distribution, or local equipment are damaged.

It is not the default universal "mana" used to prevent every system from
operating simultaneously.

The design rule is:

> DeepRun limits the player primarily through people, readiness, time, access,
> noise, damage, and local capability rather than through an arbitrary global
> reactor power budget.

Examples:

```text
more propulsion
    -> more speed
    -> more self-noise / cavitation risk
    -> no artificial requirement to steal generic power points from sonar

more sonar effort
    -> better classification / tracking performance
    -> requires qualified sonar personnel and attention
    -> may reduce capacity for other sonar tasks

more damage control
    -> faster containment / repair
    -> consumes available qualified crew
    -> may require an accessible route to the casualty
    -> leaves fewer people for other stations or emergencies

weapon preparation
    -> improves readiness
    -> requires time and qualified weapons personnel
    -> may compete with other crew tasks

electrical casualty
    -> local or ship-wide electrical capacity becomes genuinely constrained
    -> some systems may be unavailable, degraded, or mutually exclusive
```

Where electrical limits matter, they should emerge from the actual damaged or
configured submarine state rather than from a generic balancing bar.

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

## 7. Compartments, watertight boundaries, and access

The submarine is divided into gameplay compartments connected by meaningful
access boundaries.

A compartment may expose state such as:

```text
integrity
flooding
fire
smoke / toxic atmosphere
air quality
temperature
pressure where gameplay-relevant
power availability
important installed systems
crew presence
repair status
```

The game must not assume that every watertight door or hatch is permanently
closed during normal operation.

Normal access configuration may leave selected boundaries open for movement,
communication, and work.

A casualty can require the crew to establish a watertight or fire boundary.

### 7.1 Boundary isolation

Gameplay-level isolation may involve more than closing one visible door.

Conceptually it can include:

```text
watertight door / hatch
ventilation isolation
piping / stop valves
other modeled cross-boundary paths
```

The exact set is abstracted to the level required for gameplay.

A player command such as:

```text
SealBoundary
```

means "establish the required modeled boundary", not "magically toggle every
real valve on a submarine".

### 7.2 Boundary state

A modeled boundary can have state such as:

```text
Open
Closing
Closed
Leaking
JammedOpen
JammedClosed
Destroyed
```

Exact names are deferred until Milestone 6.

Damage, obstruction, deformation, pressure differential, fire, or flooding may
prevent normal operation.

Closing a boundary is therefore an action with time, access, and failure risk,
not an instantaneous universal switch.

### 7.3 Access graph

Crew movement is constrained by an authoritative compartment-access graph.

A route can become unavailable because of:

```text
sealed watertight boundary
jammed closure
heavy flooding
dangerous pressure differential
fire / extreme heat
dense smoke / toxic atmosphere
structural damage
loss of required breathing protection
```

This means that a qualified specialist may exist on board but still be unable
to reach the casualty.

The UI should communicate:

```text
task location
available routes
blocked boundaries
estimated travel time
required protective equipment
risk
```

without forcing the player to manually path every sailor through every hatch.

### 7.4 Isolation trade-off

Sealing a damaged compartment can save the submarine while creating a new
problem.

Example:

```text
breach
    -> flooding increases
    -> boundary is ordered sealed
    -> progressive flooding is limited
    -> access route is lost
    -> crew or equipment may remain isolated
    -> rescue / repair options become more difficult
```

The game must allow the possibility that people are trapped on the wrong side
of a boundary.

Opening a boundary later may:

```text
restore access
allow rescue
allow repair
```

but can also:

```text
spread flooding
spread smoke / toxic products
destroy an established fire boundary
expose adjacent spaces to pressure / heat
```

This is a commander decision, not a simple "open is good / closed is good"
toggle.

### 7.5 Player interaction level

The player should issue meaningful orders such as:

```text
seal damaged compartment
attempt emergency access
route team through alternate path
rescue isolated crew
maintain boundary
re-open after conditions improve
```

The player must not be required to click every individual hatch or ventilation
valve unless a future scenario makes that specific interaction genuinely
interesting.

## 8. Flooding, buoyancy, and compartment atmosphere

Flooding is not merely "submarine HP".

Water mass must influence the physical vessel state at the level required by
gameplay.

Possible consequences:

```text
increased mass
trim change
loss of reserve buoyancy
increased depth tendency
reduced manoeuvrability
system failures
crew hazards
loss of access
pressure changes where relevant
```

Damage control can counter these effects through:

```text
isolation
pumping
ballast actions
propulsion / depth control
repair
```

Emergency actions should have costs in time, noise, access, crew availability,
or future capability.

### 8.1 Local atmosphere

An isolated compartment does not immediately "run out of oxygen".

At gameplay-relevant fidelity, local habitability can depend on:

```text
oxygen
carbon dioxide
smoke
toxic contaminants
temperature
pressure
number and condition of occupants
available atmosphere-control equipment
available emergency breathing air
```

Oxygen percentage alone must not be used as the universal survival timer.

Carbon-dioxide accumulation, fire products, smoke, heat, or toxic contaminants
may become the dominant hazard before oxygen depletion.

### 8.2 Atmosphere-control capabilities

The game may model capabilities such as:

```text
normal atmosphere circulation
oxygen addition / generation
carbon-dioxide removal
atmosphere monitoring
emergency breathing air
portable / local emergency protection
```

These are capabilities rather than promises that every compartment remains
habitable under every casualty.

Emergency breathing air may keep personnel capable of movement or emergency
work in an otherwise unbreathable environment.

It does not automatically:

```text
remove smoke
remove heat
restore visibility
repair damage
make flooding safe
make an inaccessible route accessible
```

### 8.3 Fire and smoke

Fire can couple several systems:

```text
fire
    -> heat
    -> smoke / toxic products
    -> equipment damage
    -> visibility / access degradation
    -> atmosphere degradation
    -> possible need to isolate ventilation / compartment
```

A fire boundary may conflict with rescue or repair access.

The player may therefore face:

```text
keep compartment isolated
    -> limits spread
    -> isolated crew remain at risk

open boundary for rescue / attack on fire
    -> restores access
    -> risks smoke / heat spread
```

### 8.4 Disabled-submarine survival

If a compartment or the whole submarine becomes isolated from normal
atmosphere-control capability, survivability is determined by the remaining
habitable volume, occupants, contamination, emergency systems, and damage state.

This is not represented by a single fixed countdown.

The UI may present an estimate such as:

```text
Habitability: Stable
Habitability: Degrading
CO2: Elevated
Smoke: Severe
Emergency breathing: Available
Estimated safe occupancy: Uncertain / Limited
```

Exact hidden state may be more detailed than the player-facing estimate.

### 8.5 Continuous state and probability

DeepRun should not resolve atmosphere, flooding, or access by repeatedly
rolling arbitrary random checks every second.

Prefer deterministic or rate-based state where practical:

```text
water ingress rate
pump removal rate
oxygen consumption
CO2 generation / removal
smoke / contaminant accumulation
temperature trend
travel time
repair progress
```

Use deterministic seeded probability for discrete uncertain outcomes such as:

```text
closure fails after structural damage
damaged seal leaks
crew member is injured during hazardous access
improvised repair fails or degrades
damaged equipment fails under load
```

Probabilities must be derived from understandable state and tuned data.

The player should normally see qualitative risk and evidence rather than raw
percentages.

Examples:

```text
Closure integrity: POOR
Access risk: EXTREME
Atmosphere: UNBREATHABLE
Improvised repair: HIGH RISK
```

This preserves uncertainty without turning the simulation into arbitrary dice
rolls.

## 9. Crew model

DeepRun simulates important crew members individually, but the player primarily
commands teams, roles, watches, and tasks.

This distinction is authoritative:

```text
simulation
    = individual crew members and their state

player interaction
    = primarily team / role / watch level
```

The game must not require FTL-style room-by-room movement of every sailor.

### 9.1 Crew member state

A gameplay-relevant crew member may contain:

```text
identity
rank
primary specialty
secondary specialties
qualifications
skill / experience
current watch / assignment
current task
availability
health / incapacitation
fatigue / readiness
current compartment
protective equipment state where relevant
```

Exact data structures are deferred until Milestone 6.

Rank, specialty, qualification, and experience are distinct concepts:

```text
rank
    -> authority / leadership role

specialty
    -> professional domain

qualification
    -> tasks / stations the person is currently capable of performing

experience
    -> effectiveness, speed, reliability, or judgement within that capability
```

A senior officer is therefore not automatically the best person to repair a
specific electrical, hydraulic, sonar, or weapon-system casualty.

### 9.2 General damage-control capability

Crew specialization must not imply that everyone except one specialist becomes
helpless during an emergency.

Most crew represented by the game may possess baseline common damage-control
capability such as:

```text
basic firefighting
basic flooding response
isolation
assistance
casualty response
moving equipment / supplies
supporting a qualified specialist
```

Specialized restoration of complex equipment can require appropriate
qualifications.

### 9.3 Crew tasks

A task may define requirements such as:

```text
required specialty
minimum qualification
required personnel
optional supporting qualifications
location
required access route
required protective equipment
duration
priority
risk
required equipment / system state
```

Examples:

```text
contain compartment flooding
establish watertight boundary
rescue isolated crew
repair electrical bus
restore pump
repair sonar processing equipment
prepare weapon
operate a critical watchstation
perform an improvised bypass
```

Task resolution must answer:

```text
can the task be attempted?
who is eligible?
can they reach the casualty?
what protection is required?
how long will it take?
what effectiveness is expected?
what risk exists?
which other duties become undermanned?
```

### 9.4 Proper, degraded, and improvised work

Not every casualty should be binary "repairable / impossible".

Where gameplay benefits, a task may support:

```text
proper repair
    -> correct qualifications
    -> high reliability
    -> restores intended capability

degraded / bypass repair
    -> partial qualification or alternate expertise
    -> reduced capability
    -> longer time and/or higher failure risk

isolation / containment
    -> broad damage-control qualification
    -> system remains unavailable
    -> prevents a worse secondary consequence
```

A missing or unreachable specialist should therefore create a difficult
operational problem rather than always producing a dead-end button.

### 9.5 Team formation

The UI may automatically build the best eligible and reachable team for a task.

Example:

```text
Assign best available team
```

The player may optionally inspect or override the proposed assignment when a
specific specialist must be preserved for another critical duty.

This supports meaningful decisions without requiring constant individual
micromanagement.

### 9.6 Scarcity and overlap

Crew gameplay is driven by overlapping demands.

Example:

```text
the only highly qualified electrical specialist
    -> is maintaining a critical watch

a casualty requires that specialist
    -> reassign them
    -> original station becomes degraded

but the casualty compartment is isolated
    -> alternate route may be unavailable
    -> emergency access may expose the team to smoke / flooding
```

The central pressure is:

> Too many simultaneous problems for the available qualified and reachable
> people.

### 9.7 Injury, loss, isolation, and persistence

Injury or incapacitation matters through lost capability, not merely through a
smaller crew counter.

Isolation matters in the same way.

Example:

```text
Senior Sonar Operator isolated behind a sealed boundary
    -> operator is alive
    -> operator is currently unavailable to the sonar watch
    -> classification capability is degraded
```

Likewise, losing or isolating a specialist can turn later repairs into degraded
or improvised work.

Long-term named-character progression, replacement crew, training, psychology,
and campaign persistence belong to later design work and are not required for
the first Milestone 6 implementation.

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

No compartments, power allocation, crew, tactical pause, sonar combat, or
command queue.

### M3 — Underwater Environment

Presentation/environment milestone.

Do not introduce submarine-management systems merely to populate the
environment.

### M4 — Acoustic Playground

Prove:

```text
the world emits sound
the submarine observes rather than knows
contacts / tracks carry uncertainty
passive and active sonar produce meaningful tactical information
```

A minimal read-only tactical presentation is allowed as needed to debug and
play the acoustic slice.

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
watertight / fire boundaries
authoritative access graph
flooding
local atmosphere / habitability
fire / smoke / contaminants
emergency breathing capability
pumps
gameplay-relevant electrical distribution
system damage
individual authoritative crew roster
specialties and qualifications
crew reachability
task requirements
protective-equipment requirements
team formation and assignment
basic fatigue / readiness
injury / incapacitation
proper vs degraded / improvised repair
seeded discrete casualty risks
noise consequences
systems UI
```

### M7 — Cascading Failure Scenario

Prove that the systems create a compelling emergency chain including isolation,
access, specialist scarcity, atmosphere, and rescue trade-offs.

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

Expand the already proven loop with advanced threats and combined-force
systems.

## 21. Success test

The command layer succeeds when a player can face a situation such as:

```text
contact detected
    -> classify under uncertainty
    -> choose whether to reveal the submarine
    -> prepare attack
    -> enemy reacts
    -> submarine is hit
    -> one compartment floods and becomes hazardous
    -> commander orders a watertight boundary established
    -> progressive flooding is limited
    -> crew / specialist on the far side becomes isolated
    -> local atmosphere or smoke begins to degrade
    -> another casualty requires that specialist
    -> player chooses rescue, alternate specialist, or degraded repair
    -> local electrical damage may constrain available equipment
    -> pump / repair actions add noise or consume crew attention
    -> player chooses between continuing the fight and escaping
```

Every step must follow from understandable state and player decisions rather
than scripted exceptions.

Randomness may influence discrete damaged-state outcomes, but the resulting
situation must remain explainable from the casualty, access, crew, atmosphere,
and equipment state.
