# Milestone 4 — Acoustic Playground

Status: COMPLETE

Milestone 4 proves the first bounded acoustic-perception vertical slice on top of the production Antey runtime established by IG1. Acoustic simulation, perceived-world evidence, and developer ground truth remain separate authorities.

## Accepted foundation

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

The normal windowed playground uses the one-way authority chain:

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

The representative remote continuous emitter used by the M4 playground is simulation-only scenario truth. Its identity is absent from `AcousticObservation`, `SensorObservation`, `Contact`, and `Track`. The live scenario intentionally crosses the authored thermocline so the environment path is exercised rather than bypassed.

The fixed-update callback has an explicit discrete-time boundary: body position/velocity is the current authoritative Jolt state before the Engine's following `PhysicsWorld::Step`, while shaft RPM has already been committed by the successful propulsion transaction for that fixed update. M4 treats this as one bounded gameplay snapshot, not continuous acoustic/propulsion integration.

## Read-only tactical/debug presentation

The bounded Game-side runtime readout reports the first passive acquisition and first confirmed track. It exposes bearing plus bearing uncertainty and confidence; the passive track explicitly retains `position=unknown` and `velocity=unknown`. Cavitation and thermocline provenance are developer diagnostics only.

Ground-truth source position/range is available only through the explicit developer-only debugger accessor. It is not present in the ordinary runtime frame or perceived-world types and is not an AI, weapon, or player-knowledge API.

## Validation and acceptance

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

Final code acceptance was GitHub Actions run `34211992286` on commit `6916875dedb5e4136e301c40d217d22f7f432260`. Both `windows-debug` and `windows-release` passed configure, full build, CTest, and the real windowed `DeepRun.exe --smoke-test` step. The smoke step runs the D3D12/windowed production-Antey path with frame capture disabled only for CI robustness and fails unless the live log contains both `Passive contact acquired` and `Track confirmed`.

The immediately preceding windowed gate `34211016862` also passed the same Debug/Release configure/build/CTest/windowed-smoke matrix before the final uncertainty readout change.

## Deferred after M4

Surface reflection, bottom reflection, bounded multipath, reverberation, synthetic biological emitters, hydrodynamic wake, weapons, destroyers, explosions, and combat AI are not required for this first vertical slice. Weapon use, hostile combat entities, targeting, and tactical combat remain Milestone 5 or later scope.
