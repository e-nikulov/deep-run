# DeepRun Perception, Fog of War & Combat Intelligence

Status: Accepted design direction

Specification: D2 — Perception, Fog of War & Combat Intelligence

This document defines the player-facing knowledge model used by sonar, optics,
weapons and hostile AI. It specializes the existing M4/M5 perceived-world
architecture; it does not reopen completed milestone acceptance by itself.

The central rule is:

> No gameplay consumer may know a moving target merely because the simulation
> knows that target exists.

Ground truth belongs to physics/scenario simulation. Player UI, hostile AI and
weapons consume only observations and Tracks available to that observer.

## 1. One world, separate knowledge

Every sensor-owning side has its own perceived world.

```text
scenario / physics ground truth
        |
        +--> player sensors --> SensorObservation --> player TrackManager
        |
        `--> hostile sensors -> SensorObservation --> hostile TrackManager
```

A Track is an estimate, not an entity handle. It may contain bearing,
confidence, classification, estimated position and uncertainty, but it must not
carry hidden hostile identity or authoritative Transform data.

The player must never be given the hostile TrackManager that represents what
the enemy knows about the player.

## 2. Fog of war is contact knowledge, not black terrain

Deep Run should not copy the literal unexplored-black-map convention of an RTS.
Charts, coastline and known bathymetry may be available according to mission
content. Moving contacts are hidden until evidence exists.

Normal tactical presentation uses these states:

1. **Unseen** — no Track and no tactical contact marker.
2. **Bearing-only / tentative** — a fuzzy bearing sector or arc; no exact world
   position and no target model/name.
3. **Spatial estimate** — a ghost marker centered on the perceived position plus
   an uncertainty ellipse/radius derived from Track uncertainty.
4. **Classified** — the perceived class/type may change the icon/silhouette, but
   does not reveal an exact entity identity.
5. **Positive optical identification** — stronger visual identity evidence may be
   shown only after the optical sensor has actually resolved it.
6. **Coasting** — the last estimate remains visible but uncertainty grows and the
   presentation visibly degrades.
7. **Lost** — the tactical marker disappears.

Normal rendering must not place an exact destroyer/civilian/weapon marker at a
ground-truth coordinate merely because that entity exists in PhysicsWorld.
Developer/debug views may show truth only behind an explicit debug boundary.

## 3. Detection probability and confidence

M4 already owns deterministic propagation, SNR, uncertainty and Track ageing.
A future bounded refinement may turn the detection threshold into a
**deterministic-seeded probability band** rather than a hard binary edge.

Recommended GAME-POLICY shape:

- well below threshold -> effectively no detection;
- near threshold -> uncertain/intermittent detection;
- well above threshold -> highly reliable detection;
- repeated consistent observations raise Track confidence;
- a single marginal observation remains tentative;
- confidence decays and uncertainty grows without fresh evidence.

The stochastic sample must be seeded from stable simulation inputs so identical
replays/tests remain deterministic. Exact probability curves are gameplay
tuning and are not claims about real MGK-540 or foreign sensor performance.

The player may be shown qualitative solution quality and uncertainty. The game
must not expose a magical exact probability that the commander could not know.

## 4. Passive sonar, active sonar and optics

### Passive sonar

Passive sonar is the stealth-first information source.

It may provide:

- bearing;
- bearing uncertainty;
- confidence;
- acoustic/contact evidence;
- later motion-analysis evidence where implemented.

It does not automatically provide exact range, entity identity, or
military/civilian visual classification.

### Active sonar

Active ranging improves the spatial solution but is a deliberate exposure
choice. The outgoing pulse is an ordinary acoustic emission and can be detected
by another passive receiver before the player receives the echo.

This creates the intended trade:

```text
better range / lower uncertainty
        versus
stronger evidence of our bearing/presence to the enemy
```

### Periscope

The accepted M5 optical ladder remains canonical:

- silhouette/contact;
- type/class resolved;
- flag/markings resolved.

Optical evidence fuses into the same Track. A periscope is not a ground-truth
shortcut. Raising the mast also creates ordinary hostile visual evidence and can
strengthen the enemy's Track of the submarine.

## 5. Weapon employment under uncertainty

Weapons consume the shooter's perceived Track only.

A weapon-quality Track may still be wrong, stale, misclassified or associated
with a civilian contact. Firing on an unconfirmed contact is therefore a real
commander decision rather than a UI error state.

### Torpedo

A torpedo is the comparatively discreet attack option in the game loop.

- launch produces a bounded acoustic transient rather than an omniscient
  revelation of the submarine;
- the weapon may later be detected as a separate incoming contact;
- detecting the weapon does not automatically reveal the exact launch point;
- onboard passive/active seeker observations may refine or reacquire the target;
- a lower-quality initial solution may therefore remain tactically useful, at
  the cost of time, seeker risk and possible wrong-target acquisition.

Exact launch-source levels and real weapon-specific detection ranges are not
claimed here; they are GAME POLICY tuning.

### P-700

P-700 remains a long-range high-exposure weapon. It requires a qualified spatial
Track and never receives the target's authoritative physics identity.

A launch creates much stronger observable evidence than the discreet torpedo
case. Enemy sensors may create or strengthen a Track of the launch area/bearing,
but must still do so through normal observations with uncertainty — never by
being handed the player's Transform.

A P-700 attack therefore exchanges concealment for standoff lethality.

## 6. Hidden hostile awareness

Hostile AI uses its own TrackManager exactly as the player does.

Player actions that can feed hostile observations include:

- ordinary machinery/propulsion noise and cavitation;
- active-sonar transmissions;
- an exposed periscope mast;
- weapon launch transients;
- detected torpedoes/missiles and the inferred direction from which they came;
- later mission-specific aircraft, buoy, radar or visual sensors.

Enemy Track confidence decays and position uncertainty grows when contact is
lost. The enemy must therefore search, range, reacquire and sometimes fire on a
stale or inaccurate solution.

The player does **not** receive an `ENEMY KNOWS YOU: 73%` meter. Counter-detection
is communicated indirectly through observable behavior and crew interpretation,
for example:

- enemy active sonar appears on our sensors;
- a ship changes into a search/attack pattern;
- ASW aircraft/buoys begin appearing near our estimated area;
- an incoming weapon is detected;
- crew may report qualitative cues such as `possible counter-detection` or
  `enemy action suggests contact` when evidence supports it.

These cues are deductions from player-observable events, never reads of hidden
hostile Track state.

## 7. Cooperative P-700 salvo — GAME POLICY

Public sources support the high-level claim that salvo-fired SS-N-19/P-700
missiles could communicate in flight and coordinate target selection. Open
sources do not establish a trustworthy complete algorithm. Deep Run therefore
uses the public high-level concept only and implements its own bounded gameplay
policy.

### Salvo group

P-700 missiles launched against the same perceived Track within a bounded
launch window may join a `SalvoGroup`.

Each member retains:

- the launch Track ID;
- its own perceived aim point and uncertainty;
- its own seeker/defense outcome;
- no hostile body/entity identity.

The group may share **perceived missile observations** once airborne. Shared
information is fused into a `SalvoTrack`, never into ground truth.

### Accuracy benefit

One missile receives no cooperative bonus.

Two or more missiles can reduce the effect of initial Track uncertainty when
independent, mutually consistent observations are available. The intended
player-facing result is:

```text
1 missile  -> cheaper, more uncertainty-sensitive
2 missiles -> expensive, materially better solution resilience
3+         -> diminishing returns rather than guaranteed hits
```

The implementation should use bounded sensor fusion (for example inverse-
variance style fusion or an equivalent deterministic GAME-POLICY contraction)
with a hard sensor floor. It must not simply multiply hit probability by missile
count.

The benefit applies to **solution uncertainty/seeker continuity** only. Soft
kill, hard kill, maneuver defeat, physical collision and damage remain separate
outcomes. A salvo can still fail.

### Correlated bad information

Cooperation must not make bad initial intelligence magically correct.

- two missiles sharing the same wrong Track may reinforce a wrong search area;
- contradictory observations should block or reduce the fusion bonus;
- decoys and false contacts can contaminate the shared solution;
- positive optical identification before launch remains valuable.

This preserves the gameplay choice between firing now on uncertain intelligence
and first spending time/exposure to improve the Track.

### Future multi-target groups

When the game eventually has real surface formations with multiple Tracks, a
salvo may use perceived target distribution to avoid needless overkill and
spread missiles across qualified contacts. That remains future gameplay and
must never depend on hidden formation truth.

## 8. Player loop

A typical engagement should read as:

```text
noise / weak contact
  -> bearing-only Track
  -> repeated passive evidence
  -> uncertain spatial hypothesis
  -> optional active range (better solution, more exposure)
  -> optional periscope ID (much better classification, mast exposure)
  -> choose weapon
       torpedo: quieter / closer / seeker can refine
       P-700: standoff / high exposure / stronger Track gate
  -> choose salvo size
       one: economical, uncertainty-sensitive
       two+: more expensive, cooperative resilience
  -> launch
  -> our Track and enemy Track continue evolving independently
```

The combat decision is therefore not simply `enemy visible -> click target`.
It is an information-management problem under uncertainty.

## 9. Existing implementation that already satisfies D2

The current mainline already provides important pieces of this contract:

- M4 passive/active acoustic propagation and outgoing active-pulse detection;
- `SensorObservation -> Contact -> TrackManager`;
- confidence, bearing/position uncertainty, confirmation, coasting and loss;
- separate `playerTracks` and `destroyerTracks` in the combat runtime;
- Track-based weapon authorization without hostile identity leakage;
- P-700 uncertainty-sensitive terminal effectiveness;
- periscope staged visual identification and civilian-risk engagement;
- hostile bearing-only visual observation of an exposed periscope mast.

These pieces should be extended rather than replaced.

## 10. Required bounded implementation slices

A later implementation task should be split so accepted M4/M5 behavior remains
regression-testable:

1. **Track/Fog presentation** — tactical contact proxies, uncertainty geometry,
   coasting/lost presentation, and removal of remaining normal-play truth leaks.
2. **Probabilistic acquisition policy** — deterministic-seeded near-threshold
   detection while retaining current SNR/propagation authority.
3. **Launch-signature perception** — torpedo/P-700 launch observations routed
   through hostile sensors/Tracks with deliberately different exposure cost.
4. **Hidden hostile-awareness behavior** — AI search/reacquire decisions driven
   only by hostile Tracks; no player-visible hidden-confidence meter.
5. **P-700 SalvoGroup** — perceived-observation sharing, bounded fusion,
   diminishing returns and correlated-error tests.

Each slice must have headless deterministic tests and at least one normal-play
visual/behavior acceptance path.

## 11. Public-source boundary for P-700 cooperation

The gameplay concept is informed by public material, especially:

- Norman Friedman, `World Naval Developments`, *U.S. Naval Institute
  Proceedings*, January 1997: reports a Granit representative stating that
  salvo-fired SS-N-19 missiles communicate in flight to determine targets.
  https://www.usni.org/magazines/proceedings/1997/january/world-naval-developments
- Federation of American Scientists, SS-N-19/P-700 public overview: general
  guidance and employment background.
  https://nuke.fas.org/guide/russia/theater/ss-n-19.htm
- GlobalSecurity, SS-N-19/P-700 overview: public guidance/OTH discussion and
  explicit uncertainty around some reported details.
  https://www.globalsecurity.org/military/world/russia/ss-n-19.htm

Deep Run does not claim that its salvo-fusion mathematics, leader behavior,
probabilities, flight logic or target-allocation policy reproduce the real
classified/undocumented P-700 system.
