# DeepRun Gameplay Loop

Status: Accepted design direction

Specification: D0 — Core Game Design / Game Loop

The project-wide M/A/D/C taxonomy and specification registry are defined in
`docs/README.md`. D0 is a design contract, not a milestone or an
implementation-sequence position.

This document defines the intended gameplay rhythm for DeepRun.

It is deliberately higher-level than the technical engine specification.

---

## 1. Core loop

The primary loop is:

```text
receive objective
    -> enter operating area
    -> listen / observe
    -> detect a contact
    -> classify under uncertainty
    -> decide: avoid / shadow / attack
    -> manoeuvre and manage exposure
    -> fight or disengage
    -> handle consequences
    -> continue mission or return
```

The player is rewarded for judgement, not only destruction.

---

## 2. Moment-to-moment loop

During ordinary submerged operation:

```text
maintain depth and speed
    -> watch acoustic situation
    -> manage self-noise
    -> inspect contacts
    -> predict threats
    -> choose a route / posture
```

The game should allow periods where nothing explodes.

Tension comes from incomplete information and the possibility of detection.

---

## 3. Contact lifecycle

A useful contact flow is:

```text
signal
    -> bearing
    -> confidence
    -> probable classification
    -> track
    -> improved range / motion estimate
    -> decision
```

The player should often need to act before certainty reaches 100%.

---

## 4. Encounter decision

A detected object does not automatically become a combat encounter.

Possible player choices:

```text
avoid
remain silent
observe
shadow
change depth
change speed
use active sonar
prepare a weapon
attack
deploy a decoy
break contact
```

Different mission goals should make different choices correct.

---

## 5. Combat loop

When combat begins:

```text
maintain / improve track
    -> choose weapon
    -> prepare
    -> launch
    -> enemy reacts
    -> manoeuvre / decoy
    -> reassess contact
    -> attack again or disengage
```

Combat should be relatively short compared with search, stalking, and recovery.

---

## 6. Damage-control loop

After meaningful damage:

```text
identify casualty
    -> establish immediate boundaries
    -> evaluate flooding / fire / atmosphere
    -> determine whether the casualty is reachable
    -> identify required qualifications and protection
    -> choose rescue / containment / repair priorities
    -> assign best available reachable team
    -> stabilize depth / flooding
    -> perform proper, degraded, or improvised restoration
    -> reassess isolated crew and compartment habitability
    -> accept permanent loss where necessary
    -> decide whether the mission remains viable
```

Damage control is not a separate minigame.

Closing a boundary may save the submarine while trapping personnel or making a
critical system unreachable.

Opening it again may enable rescue or repair while risking progressive flooding,
smoke, heat, or toxic-atmosphere spread.

Damage control therefore changes the tactical situation.

## 7. Systemic decision loop

The intended systemic pressure comes from competing needs:

```text
I need this casualty repaired,
but my only qualified specialist is required elsewhere.

The specialist is available,
but the damaged compartment is isolated and currently unreachable.

I can open the boundary to rescue trapped crew,
but water or smoke may spread.

I can keep the boundary sealed,
but the isolated crew's habitability is degrading.

I need better classification,
but my senior sonar operator is already handling another high-priority track.

I need damage control,
but sending more qualified people leaves another station undermanned.

I can wake or reassign additional personnel,
but readiness and fatigue will suffer.

I can attempt an improvised repair,
but it will take longer and may restore only partial capability.

I need emergency pumping,
but the action may increase acoustic exposure.

I can keep fighting,
but another casualty may isolate or remove a specialist I cannot replace.
```

Electrical capacity remains relevant when actual distribution or generation is
damaged, but it is not the universal resource governing all systems.

The primary gameplay pressure is the submarine's limited operational capacity:

```text
qualified people
reachability
attention
time
readiness
habitability
acoustic discretion
surviving equipment
local electrical capacity
```

## 8. Tactical pause loop

When tactical pause is available:

```text
pause simulation
    -> inspect threats and submarine state
    -> issue a small set of orders
    -> resume
    -> observe consequences
```

Pause is for thinking, not for eliminating uncertainty.

Enemy truth remains hidden while paused.

---

## 9. Mission loop

A mission contains one or more operating objectives.

Examples:

```text
reconnaissance
shadow a group
reach a patrol area
intercept a target
protect an area
strike a target
survive and return
```

A mission may change because of:

```text
new contact
damage
unexpected enemy presence
fuel / battery / weapon limitations where applicable
new orders
optional opportunity
```

---

## 10. Run / campaign loop

The roguelite layer wraps missions in a longer arc:

```text
start run
    -> choose route / operation
    -> encounter
    -> gain information / resources / damage
    -> choose next route
    -> repair / upgrade when available
    -> take greater risk
    -> complete run or lose submarine / campaign state
```

The exact fiction of death, replacement vessels, crew persistence, and strategic progression can be decided later.

---

## 11. Pacing

Desired rhythm:

```text
quiet
    -> suspicion
    -> contact
    -> uncertainty
    -> commitment
    -> short violent event
    -> silence
    -> consequences
```

Avoid a design where an enemy is always on screen and weapons are fired continuously.

The contrast between silence and crisis is part of the submarine fantasy.

---

## 12. Information economy

Information is a gameplay resource.

The player can often trade:

```text
time for confidence
noise for information
position for safety
power for capability
crew attention for recovery
weapon readiness for flexibility
```

This is more important than raw reflex speed.

---

## 13. Success states

Success is broader than kills.

Possible successful outcomes:

```text
objective completed without detection
valuable contact classified
target shadowed
attack successful
enemy avoided
damaged submarine returned safely
crew / vessel saved after failed engagement
```

This supports missions appropriate to different submarine roles.

---

## 14. Failure states

Failure may include:

```text
submarine lost
unrecoverable flooding
critical depth exceeded
mission objective lost
forced abort
strategic failure in roguelite layer
```

Not every tactical retreat should be treated as total failure.

---

## 15. First systemic vertical slice

The first major proof of DeepRun is:

```text
1. Player moves through a 2.5D underwater space.
2. A hostile acoustic contact exists outside direct knowledge.
3. Passive sonar builds an uncertain track.
4. Player chooses whether to use active sonar.
5. Player attacks or is attacked.
6. A torpedo can hit the submarine.
7. The hit damages a compartment and starts flooding / smoke / fire as applicable.
8. Player establishes a boundary to contain the casualty.
9. The boundary changes the authoritative crew-access graph.
10. One useful specialist or team becomes isolated or unable to reach the casualty.
11. Local habitability degrades according to occupants, smoke, CO2/O2, heat, and available emergency systems.
12. Player chooses rescue, containment, alternate personnel, or degraded repair.
13. Electrical damage may constrain local systems.
14. Emergency pumping / repair / access actions affect acoustic exposure.
15. Another duty becomes undermanned because specialists were moved.
16. Player escapes or fails.
```

This slice spans several milestones and must not be implemented all at once.

## 16. Design test for every new feature

Before adding a feature, answer:

> Which decision in the core loop becomes more interesting because this exists?

If there is no clear answer, the feature is probably premature.
