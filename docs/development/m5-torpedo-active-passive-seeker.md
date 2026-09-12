# M5 — Torpedo Active / Passive Seeker Modes

Status: ACCEPTED / M5 CLOSURE

This closure extends the accepted M5 conventional-torpedo path with an autonomous local acoustic seeker and explicit, causal miss mechanisms. All seeker ranges, timing, spectra, turn rates and endurance values used by the game are authored **GAME POLICY**. They are not claims about classified or exact real-world torpedo performance.

## Authority boundary

The launch platform may provide the accepted perceived `Track` and initial firing solution before launch and during the bounded straight-run phase. After local seeker enable, the torpedo no longer receives live carrier Track updates.

The in-flight authority chain is:

`carrier perceived Track -> launch / onboard solution -> local acoustic observations -> local TrackManager -> seeker cue -> bounded torpedo steering -> PhysicsWorld sweep`

The local seeker never receives hostile `Transform`, target `PhysicsBodyHandle`, source identity or a privileged decoy flag. A target body identity becomes visible to weapon state only after a real `PhysicsWorld::SweepBoxClosest` collision.

## Seeker state machine

The accepted mixed seeker runtime is:

`Dormant -> PassiveSearch -> PassiveTrack -> ActiveSearch -> ActiveTrack -> Reacquire -> Exhausted`

- `Dormant`: seeker is not yet allowed to steer during the launch straight-run safety interval.
- `PassiveSearch`: the weapon listens for ordinary timestamped acoustic emissions through `AcousticWorld`.
- `PassiveTrack`: a locally perceived passive Track passes seeker quality gates and may steer the weapon.
- `ActiveSearch`: passive evidence is absent or lost long enough that the seeker may emit its own directional active pulse.
- `ActiveTrack`: a returned monostatic echo becomes ordinary ranged perceived evidence and may steer the weapon.
- `Reacquire`: a previously useful local contact has been lost; the seeker searches again without restoring live carrier guidance.
- `Exhausted`: the configured local-search window is exhausted and no fresh local cue is available.

Passive evidence is preferred when present. Active search is therefore a fallback/reacquisition tool rather than a permanently enabled omniscient sensor.

## Active seeker physics

The torpedo active seeker reuses the normal M4 acoustic simulation:

- directional beam and finite beam half-angle;
- finite sound propagation time;
- separate outbound and return path loss;
- reflection loss;
- receiver ambient noise and weapon self-noise;
- SNR threshold and confidence;
- bounded bearing/range uncertainty;
- thermocline attenuation through `AcousticEnvironment`;
- ordinary `SensorObservation -> TrackManager` perception conversion.

The outgoing active transmission is also published as an ordinary acoustic emission into hostile incoming-threat perception. Active homing is therefore not acoustically invisible: the target can perceive the ping through the same propagation, attenuation and SNR rules. No special "torpedo revealed" boolean is injected into commander UI.

## Causal miss model

M5 no longer needs a hidden percentage roll to explain a miss. A torpedo may fail to hit because one or more simulated conditions make the intercept physically fail:

1. **Insufficient passive SNR** — the target is too quiet relative to ambient noise and seeker self-noise.
2. **Acoustic propagation delay** — evidence arrives too late to support a useful intercept update.
3. **Thermocline attenuation** — the source/receiver path crosses the authored layer and loses signal/confidence.
4. **Active beam miss** — the reflector lies outside the current directional active beam.
5. **Weak or absent active echo** — two-way loss/reflection/noise leaves the echo below detection threshold.
6. **Track loss/coast** — local evidence ages until it no longer satisfies seeker quality gates.
7. **Failed reacquisition** — after contact loss, neither passive nor active search establishes a useful local cue in time.
8. **Acoustic decoy seduction** — an ordinary decoy emission may win local perceived-track selection and pull the weapon off the real target.
9. **Target maneuver / turn-authority limit** — the requested course change can exceed the authored maximum torpedo turn rate, producing geometric miss distance.
10. **Stale onboard solution** — after local seeker enable, loss of local contact does not restore live carrier updates; the weapon continues from its last onboard aim point until it reacquires something.
11. **Physical off-target collision** — a real Jolt collision with another physical body consumes the weapon at that hit instead of crashing runtime or magically transferring damage to the selected target.
12. **Finite endurance** — propulsion/energy lifetime is bounded by authored `maximumRunTimeSeconds`; expiry produces `Spent / EnduranceExpired` with no fabricated hit or damage.
13. **Pure near miss** — if the swept collision volume never intersects the target body, no impact and no damage event exists.

## Terminal outcomes

`ConventionalTorpedoTerminalReason` distinguishes at least:

- `None`
- `Impact`
- `EnduranceExpired`

A physical impact is authoritative only when `PhysicsWorld` returns a hit. Off-target hits are legitimate terminal misses relative to the selected target. Damage is applied to the combat target only when the actual physical hit body matches that target's body authority.

## Regression coverage

The M5 headless suite covers mixed passive/active seeker transitions, active-echo delay, off-beam failure, quiet-target passive failure, decoy seduction, reacquisition behavior, bounded steering, finite endurance and physical impact terminal state. The existing M4 active-sonar suite separately proves that an external passive receiver can hear an outgoing active transmission before the transmitter receives the echo, which is the acoustic primitive reused by torpedo active-ping threat detection.

Final acceptance still requires the normal full M5 gate in both Debug and Release: all registered CTest targets, windowed acoustic/combat smoke, P-700 production smoke and retained visual artifacts. No M6 compartment, flooding, crew or subsystem-damage scope is introduced here.
