# M4 Acoustic Playground

Status: ACTIVE

Baseline: `main` at `175f8b95381ef00866a4cdc55bef17d4d04e4054` (`chore: close IG1 production Antey runtime integration`).

## M4-A — coarse passive direct propagation

Status: IN PROGRESS

M4-A establishes the first headless gameplay-acoustic foundation under `Simulation/Acoustics`:

- four coarse spectral bands: very-low, low, medium, high;
- deterministic direct-path distance and propagation delay driven by `SimulationTime`;
- bounded propagation distance;
- gameplay-authored geometric spreading and per-band absorption;
- ambient noise plus receiver self-noise;
- per-band SNR, peak-SNR detection threshold and bounded confidence;
- passive bearing plus uncertainty;
- passive observations with no automatic range estimate and no ground-truth source/entity identity;
- receiver/sensor provenance is retained as a safe own-sensor semantic ID.

The M4-A numeric tuning is intentionally coarse gameplay data. It is not a claim about exact or classified real-world submarine signatures or ocean-acoustic performance.

`Engine/Audio` and miniaudio remain playback-only and are not dependencies of acoustic detection.

## M4-A.1 — production Antey acoustic composition

Status: IMPLEMENTED, CI VALIDATION IN PROGRESS

`Game/Submarine/AnteyAcousticModel.h` is the bounded Game composition layer between authoritative vessel runtime state and generic acoustic simulation values:

- body reference position -> `AcousticEmitter.positionMeters` and passive receiver reference position;
- authoritative linear velocity -> emitter kinematics and speed-derived flow/self-noise tuning;
- authoritative shaft RPM -> coarse propulsion-dependent source signature and self-noise;
- ambient noise remains an external acoustic-environment input rather than vessel-owned state;
- the passive receiver publishes canonical semantic sensor ID `MGK540_BOW_ARRAY`;
- source/entity identity is still absent from `AcousticObservation`.

The accepted production Antey content contract explicitly records `MGK540_BOW_ARRAY` as a semantic reserved region with `NO_GEOMETRIC_ANCHOR_AUTHORED`. M4-A.1 therefore does **not** invent a bow-array transform. Until a real geometric sensor anchor is authored, the receiver uses the authoritative vessel body reference position as an explicit gameplay approximation while preserving the correct semantic sensor identity.

The signature values are authored gameplay tuning only. They are deliberately not measured/classified Project 949A acoustic data.

## Tests

`DeepRunM4AcousticTests` covers:

- propagation arrival timing;
- frequency-dependent absorption;
- ambient/self-noise masking;
- passive knowledge boundary (bearing evidence, no perfect range);
- own-sensor semantic provenance;
- maximum propagation bound;
- deterministic repeated evaluation;
- invalid-configuration and invalid-receiver rejection;
- Antey `MGK540_BOW_ARRAY` semantic receiver identity;
- increasing propulsion RPM raises emitted signature;
- increasing vessel speed raises passive self-noise;
- authoritative vessel position/velocity reaches the acoustic snapshot without render dependencies;
- non-finite Antey runtime state is rejected.

Canonical local verification after configuring a preset:

```text
cmake --build --preset windows-debug
ctest --preset windows-debug
cmake --build --preset windows-release
ctest --preset windows-release
```

GitHub Actions runs the same Debug/Release configure, full build and CTest matrix on Windows Server 2025 with Visual Studio 2026.

## Explicitly deferred after M4-A/A.1

The next integration step is to compose the policy from the live production Antey body snapshot and propulsion state inside the playground fixed-step path, then feed a bounded synthetic/debug external source through `AcousticWorld` into a passive observation. This must preserve propagation delay and the player-knowledge boundary.

M4-A/A.1 does not yet add thermoclines, terrain occlusion, surface/bottom reflection, multipath, active sonar, contact/track fusion, cavitation signatures, biological emitters, AcousticDebugger UI, audio playback integration, weapons, damage, AI, or M5 work. Those remain later bounded M4 slices.
