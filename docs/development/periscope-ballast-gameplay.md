# Low-speed depth control and periscope identification gameplay

Status: CORRECTIVE CLOSURE CANDIDATE — FINAL CI / HUMAN ACCEPTANCE PENDING

This slice joins two related normal-gameplay mechanics without weakening the accepted perceived-world or physics boundaries:

1. low/zero-speed submerged depth control using bounded variable-ballast / trim authority;
2. periscope optical observation and staged visual identification, including the deliberate risk of engaging an unconfirmed civilian vessel.

## Design goals

The player must be able to approach periscope depth quietly without having to run the propellers simply to obtain vertical control. At the same time, diving planes remain hydrodynamic surfaces: they do not gain fictitious lift at zero water flow.

The player must also make a tactically meaningful choice between remaining submerged and uncertain, or exposing a periscope to obtain stronger visual identification. Acoustic certainty about bearing/range is not the same thing as knowing whether a surface contact is military or civilian.

## Scope A — low/zero-speed depth control

Canonical input remains unchanged:

- `Depth -1` = surface / nose-up intent;
- `Depth +1` = dive / nose-down intent.

At ordinary forward speed, only the stern horizontal planes provide hydrodynamic pitch authority. The production bow planes are deployment-only: they are either housed or extended and never rotate with Depth input or generate control-surface force in Deep Run.

At low forward speed a separate Game-owned variable-ballast/trim controller supplies bounded net vertical force at the vessel centre of mass:

- surface intent -> positive buoyancy tendency / upward vertical rate;
- dive intent -> negative buoyancy tendency / downward vertical rate;
- neutral depth input -> target vertical speed returns to zero, allowing trim authority to arrest residual ascent/descent;
- low-speed ballast authority fades continuously as forward speed rises;
- once the configured hydrodynamic-speed threshold is reached, this extra authority reaches zero and the diving planes carry the manoeuvre.

This is a gameplay model of variable ballast / trim compensation. It is intentionally **not** a classified simulation of Project 949A tank volumes, pump rates, valve sequencing or emergency-blow hardware.

### Required invariants

- no teleporting or direct modification of world Y;
- no fake force added to `ControlSurfaceSystem` at zero forward speed;
- Jolt remains the motion authority;
- WaterBody remains depth authority;
- ballast output is a real bounded force applied through PhysicsWorld;
- releasing depth input does not instantly stop the boat; trim authority damps vertical rate through force over time;
- deep and periscope-depth operation retains submerged trim compensation, but a deliberate continued surface command arms surfaced hydrostatic mode only in the final near-surface band (~2.8 m body-centre depth); in that mode reserve compensation is released and the boat settles by displaced volume around 2.24 m body-centre depth, keeping roughly three quarters of the main hull immersed and the mean waterline below the deployed bow planes;
- a dive command leaves surfaced mode immediately and restores submerged trim authority as displacement rises;
- no presentation-only state may change depth.

### Navigation HUD contract

The normal NAV HUD exposes the committed simulation state in player-readable terms:

- `INCREASING BUOYANCY`;
- `STABILIZING`;
- `DECREASING BUOYANCY`;
- `NEUTRAL / TRIMMED`.

The label is presentation only. It never becomes a second depth-control authority.

## Scope B — periscope / optical identification

The normal knowledge flow remains:

```text
scenario ground truth
    -> sensor simulation
    -> SensorObservation
    -> Contact / Track
    -> UI / AI / fire control
```

The periscope is an optical sensor, not a ground-truth shortcut.

### Operating envelope

The current normal gameplay contract defines `0–20 m` as the surface/periscope zone. A raised periscope may produce optical observations only while the ownship is inside the configured periscope operating-depth envelope.

A raised mast is exposed state. The destroyer's visual watch now produces an ordinary bearing-only Optical `SensorObservation` for an exposed mast inside its bounded visual envelope. The observation carries no player body/entity identity, no free range and no classification; it enters the destroyer's normal TrackManager and can therefore provoke active ranging/engagement. The player trades information quality for detectability.

Normal-play bindings:

- `P / D-pad Up` — raise or stow the production primary periscope;

Production mapping for this gameplay slice is `sail.retractable.03` / `SM_Antey_LOD0_SailDevice_08`. It was selected from direct production-GLB geometry inspection and the public Project 949A retractable-device arrangement as the primary gameplay periscope. `sail.retractable.09` / `SailDevice_17` is retained as the secondary periscope. These semantic mappings do not claim undocumented internal hardware characteristics. The primary mast uses the already-authored stowed/deployed transforms and a 2.5 s GAME-POLICY animation.
- `V / A` — attempt visual identification of the selected perceived Track.

### Staged optical observation

Raising the periscope does not reveal target truth and a distant visual contact does not immediately become a known ship type.

The current clear-day gameplay defaults are deliberately conservative policy values rather than claimed Project 949A optical specifications:

- default meteorological visibility: `20 km`;
- hard detection cap: `24 km` before horizon/weather quality limits;
- `Detected`: silhouette/contact only, no visual classification and no visual range solution;
- `TypeResolved`: up to `10 km` in the ideal clear-day profile; military-vs-civilian/type evidence may enter the Track and a stadimeter-style visual range estimate becomes available;
- `FlagOrMarkingsResolved`: up to `4 km`; flag or equivalent identifying markings are considered visually resolved.

The geographic horizon is evaluated with the public Bowditch-style standard-refraction relationship equivalent to roughly `3.92 km * (sqrt(observer height m) + sqrt(target visible height m))`. Ideal weather therefore never permits optical sensing through the Earth horizon.

These thresholds are gameplay abstractions informed by public marine-navigation/visibility physics and public historical periscope optics, not assertions about classified or undocumented `Сигнал-3` / `Лебедь-11` performance.

### Optical conditions

The sensor simulator accepts environment-owned optical conditions:

- meteorological visibility;
- ambient-light fraction;
- glare fraction;
- sea-state/spray obscuration fraction.

Detection quality and detail-recognition quality are separate. Reduced light, glare or sea-state obscuration can leave a distant silhouette visible while preventing type recognition. Bad meteorological visibility may suppress the observation entirely. These values are sensor inputs; TrackManager and weapon logic do not know weather truth directly.

The current normal playground supplies a clear-day default. A future time-of-day/weather system can publish dynamic values into the same contract without changing perception or fire-control authority.

A successful optical observation may therefore provide, depending on detail level:

- tighter bearing evidence;
- no range at silhouette-only detail;
- a bounded-uncertainty range estimate once type/class is resolved;
- confidence;
- visual classification evidence only from `TypeResolved` or better;
- a persistent Track optical-detail level distinct from military/civilian classification.

Initial surface classification vocabulary:

- `Unknown`;
- `MilitarySurfaceCombatant`;
- `CivilianSurfaceVessel`.

The observation contains no authoritative target entity/body identifier. `TrackManager` associates the evidence with an existing perceived contact by ordinary observation geometry.

Acoustic observations are forbidden from carrying visual classification or optical-identification detail. A strong sonar/ranging solution can therefore remain `Unknown`.

## Live civilian surface contact

Normal player-controlled combat contains a real civilian surface participant rather than a classification flag applied to the destroyer:

- separate Jolt body and collision identity;
- independent acoustic emitter;
- independent combat integrity;
- no weapon and no combat AI;
- neutral merchant-style presentation derived from its physical body;
- separate sensor-side truth used only inside sonar/periscope simulation;
- torpedo and P-700 physical impacts may damage/destroy the civilian body.

The civilian participant is intentionally not injected into the deterministic M5 smoke/P-700 acceptance setup, so accepted historical visual checkpoints and weapon-lifecycle gates are not silently changed by normal-play traffic.

Surface-contact truth selection is internal to the sensor simulation. Active sonar and periscope optics resolve the physical participant nearest the **selected perceived Track bearing**. The chosen body/entity identity is never copied into Contact/Track/UI.

The civilian presentation is intentionally unlabeled. Seeing a merchant-like silhouette does not cause the renderer to write `CIVILIAN`; classification still requires optical evidence through the normal sensor path.

## Civilian vessel risk / rules of engagement

This uncertainty is intentional gameplay.

A Track can be good enough for weapon employment while still lacking positive visual identification. In that state the UI shows:

`IDENTIFICATION: UNCONFIRMED — CIVILIAN RISK`

The game does **not** magically prohibit a launch merely because the contact is unconfirmed. If the player chooses to fire on a weapon-qualified unknown Track, the launch is permitted and the commander accepts the risk that the contact may be civilian.

Positive optical identification changes the decision:

- visually confirmed `MilitarySurfaceCombatant` -> normal weapon-quality gates still apply;
- visually confirmed `CivilianSurfaceVessel` -> normal fire command is inhibited by rules of engagement;
- unknown/unconfirmed -> weapon may fire if its normal Track-quality requirements are met, but UI explicitly presents civilian risk.

This ROE check is authoritative gameplay logic and is revalidated on the fire tick. It is not an advisory UI-only label.

Future mission design may deliberately alter ROE, add neutral/friendly classifications, consequences, scoring, reputation, campaign state or scripted exceptions. Those are outside this slice; the perception boundary remains the same.

## Periscope player loop

Target normal-play loop:

```text
acoustic contact
    -> estimate bearing/range under uncertainty
    -> optionally approach 0–20 m quietly using low-speed ballast/trim
    -> raise periscope
    -> cue optics from the selected perceived Track
    -> distant look may only detect a silhouette
    -> closer look resolves type/class
    -> close high-quality look may resolve flag/markings
    -> optical evidence fuses into the same Track
    -> commander decides whether to engage
    -> lower periscope / return deep
```

The intended tension is that remaining deep is safer but leaves classification uncertainty, while going shallow and exposing the mast improves identification at detection risk. Closing distance can improve identification but also raises exposure and engagement risk.

## Presentation requirements

Navigation HUD:

- current depth in metres;
- vertical speed with explicit `UP+` convention;
- throttle;
- diving-plane deflections;
- low-speed ballast/trim state.

Combat/periscope HUD:

- periscope unavailable outside operating depth;
- stowed / raised / exposed state;
- selected Track ID;
- explicit optical-detail state: `NONE`, `SILHOUETTE / TYPE UNRESOLVED`, `TYPE RESOLVED`, `FLAG / MARKINGS RESOLVED`;
- silhouette-only state remains visibly `UNCONFIRMED`;
- `UNCONFIRMED — CIVILIAN RISK` warning for weapon-qualified unknown contacts;
- explicit `CIVILIAN — FIRE INHIBITED` after positive visual identification;
- military confirmation when obtained;
- no renderer/debug label may reveal target truth before the Track does.

## Acceptance

### Low-speed depth control

From a submerged, nearly stationary boat with throttle at zero:

1. hold surface input;
2. vertical speed becomes positive and current depth decreases without propulsion thrust;
3. HUD reports `INCREASING BUOYANCY`;
4. return depth input to neutral;
5. trim authority arrests vertical rate over time instead of snapping position/velocity and HUD reports `STABILIZING` before `NEUTRAL / TRIMMED`;
6. repeat for dive intent and observe `DECREASING BUOYANCY`;
7. at ordinary forward speed the extra low-speed authority fades out and diving-plane dynamics remain authoritative.

### Periscope and civilian risk

1. passive acoustic evidence produces separate surface Tracks without military/civilian identity;
2. a weapon-quality unknown Track may be fired and is visibly marked as civilian-risk;
3. if that Track is actually the civilian participant, the physical weapon can hit and damage/destroy the civilian body;
4. below the periscope operating zone, optical identification is unavailable;
5. inside the periscope zone, raise the periscope and align it from the selected perceived bearing;
6. beyond effective horizon/visibility, no optical observation is produced;
7. between type-recognition range and detection limit, optical evidence records a silhouette but classification remains `Unknown` and no visual range is fabricated;
8. within type-recognition range, optical evidence may resolve military/civilian type and fuse into the same Track;
9. within flag/markings range, the Track records the higher optical-detail level;
10. poor visibility can suppress the observation; low light/glare/sea-state degradation can reduce detail before eliminating detection;
11. a military target remains engageable subject to weapon gates;
12. a civilian target becomes visually confirmed and normal fire is rejected by ROE;
13. acoustic evidence alone can never set visual classification or optical-identification level.

## Explicit non-goals

- classified Project 949A ballast-system replication;
- classified/undocumented Project 949A periscope performance claims;
- emergency surfacing / emergency blow procedures;
- full crew-station simulation;
- omniscient target labels;
- a full global dynamic-weather/time-of-day system in this slice; the optical sensor contract is ready to consume one;
- campaign/legal consequences for civilian casualties (future gameplay scope);
- replacing sonar or TrackManager with a periscope-specific target list.
