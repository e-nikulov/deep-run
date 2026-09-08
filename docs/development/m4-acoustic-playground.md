# Milestone 4 — Acoustic Playground

Status: IN PROGRESS

Milestone 4 proves the first bounded acoustic-perception vertical slice on top of the production Antey runtime established by IG1. It deliberately keeps acoustic simulation, perceived-world evidence, and developer ground truth as separate authorities.

## Implemented foundation

- `AcousticWorld`, `AcousticEmitter`, `AcousticReceiver`, and a bounded four-band gameplay spectrum.
- Deterministic direct-path transmission loss, frequency-dependent absorption, propagation delay, ambient noise, receiver self-noise, SNR, detection threshold, bearing, uncertainty, and confidence.
- Passive observations with no automatic range and no source/entity identity leakage.
- `SensorObservation` conversion, `Contact`, `TrackManager`, confidence, confirmation, ageing, coasting, and loss behavior.
- Authored thermocline loss and graded terrain attenuation through `AcousticPropagationModifiers`; terrain is not binary acoustic visibility.
- Active-sonar pulse, beam gate, outbound propagation, reflection loss, return propagation, round-trip delay/range, and separate `ActiveEcho` observations.
- Outgoing active transmissions are ordinary acoustic emissions and may be detected by another passive receiver before the transmitting vessel receives its own echo.
- Production Antey acoustic tuning derives emitted signature and passive self-noise from authoritative runtime velocity and shaft RPM, plus a smooth gameplay-only cavitation contribution modulated by signed depth.
- Developer-only `AcousticDebuggerSnapshot` compares ground truth against observed/estimated state without exposing ground truth through the normal observation/contact/track API.

## Live production-runtime composition

The normal windowed playground now uses the one-way authority chain:

```text
PhysicsWorld/Jolt body state
+ WaterBody signed depth
+ committed propulsion shaft RPM
        ↓
AnteyAcousticRuntimeState
        ↓
AnteyAcousticSnapshot
        ↓
AcousticWorld + authored environment modifiers
        ↓
AcousticObservation
        ↓
SensorObservation
        ↓
Contact / TrackManager
```

`PhysicalPlayground::BuildAcousticSnapshot` is read-only. Acoustics cannot write state back into Jolt, `WaterBody`, propulsion, rendering, input, or `AudioEngine`/miniaudio.

The representative remote continuous emitter used by the M4 playground is simulation-only scenario truth. Its identity is not present in `AcousticObservation`, `SensorObservation`, `Contact`, or `Track`. The current live scenario intentionally crosses the authored thermocline so the environment path is exercised rather than bypassed.

The fixed-update callback has an explicit discrete-time boundary: the body position/velocity sample is the current authoritative Jolt state before the Engine's following `PhysicsWorld::Step`, while the shaft RPM has already been committed by the successful propulsion transaction for that fixed update. M4 treats this as one bounded gameplay snapshot; it is not a claim of continuous acoustic/propulsion integration.

## Validation

`DeepRunM4AcousticTests` covers:

- delayed passive arrival and deterministic repeatability;
- frequency-dependent loss, ambient/self-noise masking, and bounded propagation;
- production Antey sensor semantic identity and runtime signature inputs;
- perception conversion and track lifecycle without omniscient target identity;
- thermocline and terrain-loss modifiers;
- active round-trip timing/range and detection of outgoing active transmission;
- cavitation source/self-noise effects and depth modulation;
- developer debugger knowledge boundary;
- live runtime bridge from body/water/propulsion snapshots to a tentative then confirmed bearing-only track.

The full Debug and Release CI matrix must remain green for every accepted M4 slice. Windowed runtime smoke remains a separate closure check because the CI workflow currently runs configure/build/CTest only.

## Remaining before M4 closure

- bounded developer/read-only tactical presentation that exposes observations, contacts/tracks, uncertainty/confidence, and debugger comparison without granting ground truth to gameplay;
- final windowed smoke/acceptance evidence for the live production-Antey acoustic path;
- final M4 integration review and milestone status transition to `COMPLETE` only after those gates are accepted.

Surface/bottom reflection, bounded multipath, reverberation, biological emitters, wake, weapons, destroyers, explosions, and combat AI are not required for this first vertical slice unless separately promoted into scope. Combat remains Milestone 5.
