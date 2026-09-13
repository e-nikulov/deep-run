# DeepRun Perception, Fog of War & Combat Intelligence

Status: IMPLEMENTED IN `design/perception-fog-salvo-awareness` / acceptance pending CI

Specification: D2 — Perception, Fog of War & Combat Intelligence

D2 defines how Deep Run represents incomplete knowledge of other participants and how that uncertainty affects commander decisions, weapon employment and enemy reactions.

The authoritative rule is:

```text
world truth
    -> sensor simulation
    -> SensorObservation
    -> Contact / Track
    -> commander / AI / weapon guidance / presentation
```

Normal gameplay must never skip directly from world truth to a target label, exact marker, fire-control solution, enemy decision or missile aim point.

## 1. Player fog of war

An undiscovered participant exists physically in the simulation but does not exist in the normal commander UI.

A contact becomes visible through evidence, not scenario membership. Presentation progresses through knowledge states:

```text
BearingOnly
    -> AreaEstimate
    -> Classified
    -> PositiveIdentification

and any stale track may become:

Coasting -> Lost
```

`BearingOnly` is a direction/sector, not a point target. `AreaEstimate` may show an estimated point plus an uncertainty region. The region is explicitly a hypothesis and must grow or degrade as the underlying Track ages.

A physical military or civilian surface vessel is not drawn from ground truth in the normal player path merely because its Jolt body exists. Direct vessel presentation becomes legal only after the player's own perceived evidence resolves the corresponding visual classification. Automated historical M5 acceptance paths retain their existing ground-truth presentation so this design change does not invalidate previously accepted visual-regression evidence.

## 2. Existing M4/M5 foundation

D2 extends rather than replaces the accepted perception architecture:

- passive sonar may create bearing-only observations without range or identity;
- active sonar can add range evidence while the outgoing transmission is itself detectable;
- `TrackManager` owns confirmation, confidence, uncertainty, ageing, coasting and loss;
- optical/periscope evidence may add staged visual detail and classification;
- weapon authority consumes perceived Tracks, never hostile body handles or authoritative hostile transforms;
- a weapon-quality unknown Track may remain a civilian-identification risk;
- confirmed civilian classification inhibits normal fire under the current ROE.

## 3. Player-facing contact presentation

Normal sonar/tactical presentation uses short knowledge labels:

```text
BRG   = bearing-only evidence
AREA  = estimated position with uncertainty
CLASS = type/class resolved by optical evidence
ID    = positive visual identification
COAST = stale prediction/no fresh evidence
```

The player may know that a contact exists while not knowing what it is or exactly where it is. A high-confidence acoustic solution does not magically become visual classification.

## 4. Hidden hostile awareness

Every hostile observer owns its own perceived Tracks. There is no shared omniscient `playerDetected` bit.

For AI/gameplay reasoning a hidden awareness state may be derived from hostile Tracks:

```text
Unaware
Suspected
Localized
FireControlQuality
```

These states are not shown numerically to the player. The player infers hostile awareness through observable behaviour such as active sonar searching toward the boat, course changes, pursuit, weapons, countermeasures and future aircraft/helo response.

The UI must not expose the hostile confidence percentage or exact hostile estimate of player position.

## 5. Exposure is evidence, not an omniscient reveal

Actions that can reveal the player create ordinary perceived evidence for hostile sensors. They never set the player's true position directly into hostile AI.

D2 GAME POLICY ranks current exposure sources approximately as:

```text
P-700 launch / active sonar transmission  -> strong long-range exposure
raised periscope mast                      -> optical exposure inside visual envelope
torpedo launch                             -> weaker/local acoustic exposure
quiet passive listening                    -> no active transmission exposure
```

The concrete ranges/probabilities used by the current bounded implementation are gameplay tuning, not claims about real sensor performance or classified weapon signatures. A detected launch observation is bearing/confidence evidence only; the observer must still build and maintain a Track through its normal perception path.

## 6. Periscope information/exposure trade-off

The accepted M5 periscope remains an optical sensor rather than a truth shortcut. Current staged detail remains:

```text
Detected
TypeResolved
FlagOrMarkingsResolved
```

At useful optical range/conditions this can resolve military-vs-civilian type and eventually positive identifying detail. A raised mast is simultaneously an exposed object that hostile visual watch may detect and feed into the hostile `TrackManager`.

Thus the commander chooses between remaining deep and uncertain or approaching periscope depth, improving identification and accepting exposure risk.

## 7. Weapon employment under uncertainty

A weapon launch is legal only if the selected perceived Track satisfies that weapon's targeting and employment requirements. The Track may still be wrong.

The design deliberately allows a commander to fire on an unconfirmed but weapon-qualified contact when current ROE permits it. The UI must communicate the risk rather than silently replacing uncertainty with truth.

A stale, poorly ranged or highly uncertain contact reduces weapon effectiveness through weapon-specific perceived-data logic rather than a hidden global hit-chance modifier.

## 8. Torpedo concealment

A torpedo is the comparatively covert attack option. Its launch may create bounded passive-acoustic exposure, and the running weapon may later be detected as an incoming threat, but neither event automatically returns the submarine's exact location.

Enemy observers infer the source through their own Track state and subsequent evidence. Launching a torpedo is therefore not consequence-free, but is materially less revealing than a P-700 launch under current GAME POLICY.

## 9. P-700 launch exposure

Launching a P-700 is intentionally a major tactical commitment. Current GAME POLICY treats the launch event as strong long-range evidence. A hostile observer that detects it receives a noisy bearing/confidence observation, not the player's body identity or authoritative coordinates.

The intended trade-off is:

```text
long-range / high-damage strike
    <->
large weapon expenditure + major exposure risk
```

This lets a successful missile attack still have strategic consequences: surviving escorts may gain a substantially better idea of where the launching submarine was.

## 10. P-700 cooperative salvo

### Public-source boundary

Open publications have described the P-700 complex at a high level as supporting information exchange and target allocation among missiles in a salvo. D2 uses only that broad concept as inspiration.

The game does not claim or reproduce a classified real-world coordination algorithm, seeker logic, leader-election protocol, datalink format, ECCM technique or exact sensor performance. Every concrete fusion rule below is explicit GAME POLICY.

### Runtime model

A salvo owns multiple real `P700GranitRuntimeState` instances. It is not a hidden `+accuracy` multiplier on one missile.

Normal player modes are:

```text
SINGLE x1
PAIR x2
```

`PAIR` consumes two loaded production launchers belonging to the same authored paired hatch group. Inventory consumption is transactional: either every member materializes and all selected launchers become `Spent`, or none are consumed.

Each missile has its own production launch anchor, full launch lifecycle, physical collision sweep and bounded noisy seeker observation. It may be defeated or impact independently.

The physical target truth is used only inside the bounded seeker-simulation boundary. Its output contains no target body/entity identity.

### Cooperative perceived-data fusion

Missile observations may be fused only when they refer to the same launch `trackId`.

Compatible observations are combined into a `SalvoTrack` using uncertainty/confidence weighting. Incompatible observations are not averaged into a fictitiously precise solution. A correlated-error floor prevents larger salvoes from driving uncertainty toward zero.

Current GAME POLICY applies diminishing returns. Two missiles can materially reduce uncertainty when their independent evidence agrees, but do not guarantee a hit. Terminal seeker failure, deception, hard-kill interception, maneuver defeat and physical collision still resolve independently through the existing weapon runtime.

Therefore:

```text
SINGLE
    -> one missile spent
    -> lower attack cost
    -> one airborne seeker view
    -> more dependence on launch Track / one seeker solution

PAIR
    -> two missiles spent
    -> greater launch/exposure commitment
    -> two independent seeker observations
    -> cooperative fusion when observations agree
    -> higher resilience to uncertainty, not guaranteed success
```

## 11. Repeated missile use

The 24-slot production inventory is persistent within the current runtime session. Once every missile of a launched salvo reaches a resolved terminal state and becomes `Spent`, the commander weapon profile returns from `Launched` to `Stored` while the launcher inventory remains consumed.

This makes the production inventory real gameplay state rather than a one-launch-per-session facade.

## 12. Normal controls

Current bounded controls add:

```text
G / D-pad Down -> toggle P-700 SINGLE / PAIR
```

Existing weapon select, prepare and fire controls are unchanged.

## 13. Authority invariants

The following are forbidden:

- rendering a normal-play target marker from scenario truth;
- exposing hostile body/entity identity through normal Track/UI APIs;
- passing the true target transform into fire-control merely because a target exists;
- showing the player the enemy's exact awareness/confidence state;
- making Pair mode an invisible probability bonus without simulating both missiles;
- allowing cooperative missile fusion to switch to a different Track identity;
- using acceptance/debug ground truth as shipping player knowledge.

## 14. Acceptance requirements

D2 is accepted only when Debug and Release CI preserve all prior M4/M5 gates and additionally prove:

1. bearing-only Track projects to `BRG` without a fabricated position;
2. ranged perceived evidence produces `AREA` plus uncertainty;
3. visual classification produces `CLASS/ID` without acoustic truth leakage;
4. hostile awareness is derived from hostile Tracks and remains hidden from player UI;
5. P-700 launch exposure is stronger than torpedo-launch exposure under deterministic GAME POLICY sampling;
6. exposure observations contain no free exact range or classification;
7. Pair mode selects one real authored two-missile hatch group;
8. invalid pair inventory operations are transactional;
9. two P-700 runtime members launch from distinct production anchors;
10. mutually consistent independent seeker observations improve the shared perceived solution;
11. conflicting observations do not fabricate precision;
12. different Track identities cannot be fused;
13. every salvo member remains independently collidable/defeatable;
14. normal player presentation does not draw undiscovered surface vessels from ground truth;
15. all historical M5 acceptance/smoke paths remain green.
