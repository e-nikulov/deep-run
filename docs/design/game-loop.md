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
identify problem
    -> contain spread
    -> choose power / crew priorities
    -> stabilize depth / flooding
    -> restore critical capability
    -> accept permanent loss where necessary
    -> decide whether the mission remains viable
```

Damage control is not a separate minigame.
It changes the tactical situation.

---

## 7. Systemic decision loop

The intended systemic pressure comes from competing needs:

```text
I need more speed
but speed raises noise.

I need pumps
but pumps consume power and may raise noise.

I need better sonar information
but active transmission reveals me.

I need repairs
but the damage-control team cannot be everywhere.

I can keep fighting
but another hit may make return impossible.
```

The game should repeatedly generate choices of this form.

---

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
7. The hit damages a compartment.
8. Flooding affects the vessel.
9. Power becomes constrained.
10. The player activates pumps / reassigns crew.
11. Emergency actions affect acoustic exposure.
12. The player escapes or fails.
```

This slice spans several milestones and must not be implemented all at once.

---

## 16. Design test for every new feature

Before adding a feature, answer:

> Which decision in the core loop becomes more interesting because this exists?

If there is no clear answer, the feature is probably premature.
