# Low-speed depth control and periscope identification gameplay

Status: ACCEPTED / M5 COMPLETE

Final corrective acceptance is anchored by PR #11 head `0efd5ff2eaa6fa504c2f60a02a381cec93331626` and GitHub Actions run `34775791243` (#770). Both Windows Debug and Release passed production Antey contract validation, configure, build, all registered CTest targets, windowed acoustic smoke, P-700 production launch smoke, and M5/P-700 acceptance-artifact retention. The accepted tree was merged to `main` as `678201074d093fb71806101cf21be375d0e271fc`.

This slice joins two related normal-gameplay mechanics without weakening the accepted perceived-world or physics boundaries:

1. low/zero-speed submerged depth control using bounded variable-ballast / trim authority;
2. periscope optical observation and staged visual identification, including the deliberate risk of engaging an unconfirmed civilian vessel.

## Design goals

The player must be able to approach periscope depth quietly without having to run the propellers simply to obtain vertical control. At the same time, diving planes remain hydrodynamic surfaces: they do not gain fictitious lift at zero water flow.

The player must also make a tactically meaningful choice between remaining submerged and uncertain, or exposing a periscope to obtain stronger visual identification. Acoustic certainty about bearing/range is not the same thing as knowing whether a surface contact is military or civilian.

## Physical ballast / hydrostatic authority

Deep Run uses one Archimedean model across surfaced and submerged operation. The production player submarine is explicitly Project 949A `Antey` / Oscar II. Runtime hydrostatics now use the RussianShips Project 949A displacement pair directly: **14,820 t surfaced / 19,254 t submerged**. Deepstorm/Apalkov's 14,700 / 19,400 (24,000?) t and other conflicting public pairs remain reference/provenance data rather than a second physics authority. Main-ballast mass delta is **4,434 t** (~29.92% of surfaced displacement); at 1025 kg/m^3 the fixed full displaced volume is ~**18,784.390 m^3**. Jolt rigid-body mass and inertia change with ballast fill and with weapons physically leaving the boat. With no buoyancy-point/render/body offset and no visual waterline target, empty main ballast settles naturally at ~**2.34648 m** body-reference depth under the existing production waterplane response; full main ballast is neutrally buoyant when fully immersed. There is no surfaced-mode buoyancy switch and no reserve-compensation vertical force. In ordinary submerged manoeuvring the main ballast remains flooded: quiet depth changes use bounded trim-water mass at low speed and the stern planes at hydrodynamic speed. Normal main-ballast blowing is armed in the final near-surface band. To prevent a surface-command deadlock, continuing Surface also starts the blow if low-speed trim has actually reached its maximum-buoyancy limit and the controller still requests that same maximum; a normal deep ascent that is still making vertical progress therefore does not blow main ballast. A dive command floods any partly empty main ballast.

Exact Project 949A flood/blow timing is not asserted from public data; the current 40 s full-range transition is explicit GAME POLICY. Small low-speed trim authority is represented as bounded equivalent water mass, not as a direct vertical force. Weapon launches reduce rigid-body mass by the expended P-700/torpedo round mass; dedicated compensation water restores that mass with a finite 2.8 t/s GAME-policy slew while keeping hull displaced volume fixed. Full-ahead propulsion and immersion-dependent quadratic drag are calibrated to the public 32 kn submerged / 15 kn surfaced canonical without hard velocity clamps. Public sources do not provide a trustworthy Project-949A-specific maximum vertical rate; generic open literature for nuclear submarines quotes roughly 6–9 m/s, so Deep Run does not label that range as an Antey-specific TTX. The current source-first 19,254 t model independently produces ~6.87 m/s as the still-water terminal rise under maximum positive buoyancy and ~6.96 m/s as the 32 kn / 25-degree full-command hydrodynamic trajectory.

## Scope A — low/zero-speed depth control

Canonical input remains unchanged:

- `Depth -1` = surface / nose-up intent;
- `Depth +1` = dive / nose-down intent.

At ordinary forward speed, only the stern horizontal planes provide hydrodynamic pitch authority. The production bow planes are deployment-only: they are either housed or extended and never rotate with Depth input or generate control-surface force in Deep Run.

At low forward speed a Game-owned trim controller requests a bounded **equivalent water-mass change** rather than applying a vertical force. At higher speed that trim authority fades and the stern horizontal planes carry the manoeuvre. Main-ballast fill is physical mass state: with full configured combat load, empty is 14,820 t and full is the source-backed 19,254 t Project 949A submerged mass. Successful weapon launches subtract real ordnance mass until compensation water replaces it.

- submerged surface intent -> reduce bounded trim-water mass; sustained intent in the final surface band may then blow main ballast, and saturated/stalled maximum-buoyancy trim escalates to main-ballast blow even if the boat stalls just outside that band;
- submerged dive intent -> increase bounded trim-water mass while the filled main ballast stays filled; from a surfaced/partly blown state the same intent floods main ballast;
- neutral depth input -> trim target returns toward neutral and residual vertical motion is arrested by physical drag plus finite trim-mass correction;
- low-speed trim authority fades continuously as forward speed rises;
- at hydrodynamic speed the stern planes and the hull's static pitch stability determine the vertical trajectory.

This is a gameplay-level physical ballast model. It intentionally **does not** claim Project 949A tank volumes, valve sequencing, pump/blow rates or emergency-blow timing that are not established by public data.

### Required invariants

- no teleporting or direct modification of world Y;
- no fake lift from a control surface at zero water flow;
- no direct ballast/reserve-compensation vertical force;
- Jolt remains motion, mass and inertia authority;
- WaterBody remains depth and displaced-water authority;
- main-ballast/trim commands change physical rigid-body mass with finite GAME-policy rates;
- ordinary deep/periscope-depth commands do not blow the main ballast tanks while bounded trim still has effective authority; a held Surface command may escalate only after maximum-buoyancy trim is saturated and still demanded;
- buoyancy is produced only by displaced water through `BuoyancySystem`;
- with full main ballast the fully immersed 19,254 t Project 949A source state is neutrally buoyant;
- with empty main ballast the 14,820 t Project 949A source state settles naturally at ~2.34648 m body-reference depth in flat water;
- every successful P-700/torpedo launch reduces rigid-body mass by the actual configured round mass;
- dedicated weapon-compensation water then restores the expended mass with finite actuator rate;
- no buoyancy-point, render, camera or body-position offset participates in surfaced equilibrium;
- stern-plane moment is opposed by speed-squared static pitch stability, so held input converges to a trajectory instead of allowing endless pitch rotation;
- no presentation-only state may change depth.

### Navigation HUD contract

The normal NAV HUD exposes the committed simulation state in player-readable terms, including actual axial speed, main-ballast fill percentage, live main-ballast state (`FULL`, `BLOWING`, `FLOODING`, `HOLDING`, `EMPTY`), signed trim-water mass and total physical mass, plus expended-ordnance / weapon-compensation water mass and the semantic trim state. While Surface is held and main ballast is actively blowing, the HUD explicitly shows `Surface hold (W / LS UP): MAIN BALLAST BLOW`.

The semantic trim state remains:

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

Direct production-GLB inspection plus the public Project 949A retractable-device arrangement identifies private authoring node `SailDevice_10` as the PZNS-10S gameplay primary periscope; the SIGNAL-3 node remains intentionally unbound until explicitly confirmed. Those node names do not cross into normal runtime: generated sidecars publish `PERISCOPE_PRIMARY` / `PERISCOPE_SECONDARY`, the production loader requires exactly one of each, and gameplay resolves the primary mast by semantic role. No disputed exact historical optics-model designation is asserted. The primary mast uses the already-authored stowed/deployed transforms and a 2.5 s GAME-POLICY animation.
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
