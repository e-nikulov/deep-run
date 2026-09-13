# Low-speed depth control and periscope identification gameplay

Status: IMPLEMENTATION IN PROGRESS

This slice joins two related normal-gameplay mechanics without weakening the accepted perceived-world or physics boundaries:

1. low/zero-speed submerged depth control using bounded variable-ballast / trim authority;
2. periscope optical observation and visual identification, including the deliberate risk of engaging an unconfirmed civilian vessel.

## Design goals

The player must be able to approach periscope depth quietly without having to run the propellers simply to obtain vertical control. At the same time, diving planes must remain hydrodynamic surfaces: they do not gain fictitious lift at zero water flow.

The player must also be able to make a tactically meaningful choice between remaining submerged and uncertain, or exposing a periscope to obtain stronger visual identification. Acoustic certainty about bearing/range is not the same thing as knowing whether a surface contact is military or civilian.

## Scope A — low/zero-speed depth control

Canonical input remains unchanged:

- `Depth -1` = surface / nose-up intent;
- `Depth +1` = dive / nose-down intent.

At ordinary forward speed, bow/stern diving planes remain the primary pitch/depth-control mechanism and their force continues to emerge from local water flow.

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
- no presentation-only state may change depth.

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

A raised mast is itself exposed state. It will feed the existing signature/detection architecture as an optical-mast/periscope exposure channel when hostile visual sensing is implemented. The player therefore trades information quality for detectability.

### Optical observation

A successful optical observation may provide:

- much tighter bearing evidence;
- range estimate suitable for visual ranging gameplay;
- confidence;
- visual classification evidence.

Initial surface classification vocabulary:

- `Unknown`;
- `MilitarySurfaceCombatant`;
- `CivilianSurfaceVessel`.

The observation contains no authoritative target entity/body identifier. `TrackManager` associates the evidence with an existing perceived contact by ordinary observation geometry.

Acoustic observations are forbidden from carrying visual classification evidence. A strong sonar/active-ranging solution can therefore remain `Unknown`.

## Civilian vessel risk / rules of engagement

This uncertainty is intentional gameplay.

A Track can be good enough for weapon employment while still lacking positive visual identification. In that state the UI must clearly show an identification warning such as:

`IDENTIFICATION: UNCONFIRMED — CIVILIAN RISK`

The game does **not** magically prohibit a launch merely because the contact is unconfirmed. If the player chooses to fire on a weapon-qualified unknown Track, the launch is permitted and the commander accepts the risk that the contact may be civilian.

Positive optical identification changes the decision:

- visually confirmed `MilitarySurfaceCombatant` -> normal weapon-quality gates still apply;
- visually confirmed `CivilianSurfaceVessel` -> normal fire command is inhibited by rules of engagement;
- unknown/unconfirmed -> weapon may fire if its normal Track-quality requirements are met, but UI explicitly presents civilian risk.

This ROE check is authoritative gameplay logic and must be revalidated on the fire tick. It is not an advisory UI-only label.

Future mission design may deliberately alter ROE, add neutral/friendly classifications, consequences, scoring, reputation, campaign state or scripted exceptions. Those are outside this slice; the perception boundary remains the same.

## Periscope player loop

Target normal-play loop:

```text
acoustic contact
    -> estimate bearing/range under uncertainty
    -> optionally approach 0–20 m quietly using low-speed ballast/trim
    -> raise periscope
    -> point optics at selected Track
    -> obtain optical observation if geometry/range permit
    -> Track gains visual classification evidence
    -> commander decides whether to engage
    -> lower periscope / return deep
```

The intended tension is that remaining deep is safer but leaves classification uncertainty, while going shallow and exposing the mast improves identification at detection risk.

## Presentation requirements

Navigation HUD:

- current depth in metres;
- vertical speed with explicit `UP+` convention;
- throttle;
- diving-plane deflections;
- low-speed ballast/trim authority and/or commanded vertical rate where useful for acceptance.

Combat/periscope presentation:

- periscope unavailable outside operating depth;
- stowed / raised / exposed state;
- selected Track ID and visual-ID status;
- `UNCONFIRMED — CIVILIAN RISK` warning for weapon-qualified unknown contacts;
- explicit `CIVILIAN — FIRE INHIBITED` after positive visual identification;
- military confirmation when obtained;
- a periscope view must remain a presentation of optical sensor state, never a debug ground-truth camera.

## Acceptance

### Low-speed depth control

From a submerged, nearly stationary boat with throttle at zero:

1. hold surface input;
2. vertical speed becomes positive and current depth decreases without propulsion thrust;
3. return depth input to neutral;
4. trim authority arrests vertical rate over time instead of snapping position/velocity;
5. repeat for dive intent;
6. at ordinary forward speed the extra low-speed authority fades out and diving-plane dynamics remain authoritative.

### Periscope and civilian risk

1. acoustic evidence can create a weapon-quality Track whose classification remains `Unknown`;
2. firing that Track is possible and visibly marked as civilian-risk;
3. above the periscope operating depth, optical identification is unavailable;
4. inside the periscope zone, raise the periscope and align it with the selected contact;
5. optical observation fuses into that Track without exposing target entity identity;
6. a military target becomes visually confirmed and remains engageable subject to weapon gates;
7. a civilian target becomes visually confirmed and the normal fire command is rejected by ROE;
8. acoustic evidence alone can never set either visual classification.

## Explicit non-goals

- classified Project 949A ballast-system replication;
- emergency surfacing / emergency blow procedures;
- full crew-station simulation;
- omniscient target labels;
- perfect optical visibility through weather/night/sea-state in this first slice;
- campaign/legal consequences for civilian casualties (future gameplay scope);
- replacing sonar or TrackManager with a periscope-specific target list.
