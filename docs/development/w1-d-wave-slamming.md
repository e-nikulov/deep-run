# W1-D — Wave Slamming / Impact Exposure

Status: **ACCEPTED** on 2026-09-15.

## Scope

W1-D adds deterministic surface re-entry / wave-slamming evidence for the production submarine without
introducing M6 structural damage, flooding, or crew-casualty logic early.

The authoritative wave surface remains the W1-B production spectrum and W1-C remains the only system that
applies wave-dependent hydrostatic force to the submarine. W1-D observes the same per-point hydrostatic
samples and produces an impact event; it does not add another force model.

## Marine contract

`Simulation/Marine/SurfaceImpactSystem` is a pure Simulation component. It has no Engine/Physics, Render,
input, damage, or crew dependency. Game adapts W1-C buoyancy samples into the Marine-owned
`SurfaceImpactPointSample` value.

A longitudinal sample must first emerge below a configured rearm submerged fraction and subsequently
re-enter through the trigger fraction. This hysteresis prevents one crest from producing an event every
fixed tick.

For a qualifying re-entry the system publishes:

- production longitudinal point index and world position;
- positive relative wetting speed from signed-depth change over fixed SimulationTime;
- `q = 0.5 * rho * v^2` as a dimensional dynamic-pressure **proxy/evidence value**, not a claimed local
  structural peak pressure;
- bounded `[0,1]` severity for presentation/gameplay consumers.

The current Game policy uses 0.55 rearm fraction, 0.65 trigger fraction, 1 m/s minimum wetting speed and
6 m/s severe-speed endpoint. These are gameplay/presentation tuning and are not Project 949A structural TTX.

## Production runtime

`PhysicalPlayground` evaluates W1-D against all four production longitudinal buoyancy points. Impact history
is double-buffered and is committed only at the existing fixed-tick transaction boundary. If any later
calculation or force application fails, the previous committed impact history remains authoritative.

When several points impact during one tick, the strongest event is selected for semantic feedback. The Game
layer maps it to a separate short, high-priority `WaveSlam` haptic event. Existing continuous engine-vibration
feedback remains independent.

W1-D does **not** directly reduce hull HP, create flooding, or injure crew. Future M6 systems may consume the
dimensional impact evidence together with authored compartment/structure state.

## Acceptance

The production/code acceptance head is `88f49ca475dfc6acafc70ef9faaf08973ec81d4a`. GitHub Actions run
`34985029653` passed in both Debug and Release:

- configure/build;
- production asset/source-tree staging gate;
- 10/10 CTest targets;
- `DeepRunW1SurfaceDynamicsTests`, including integrated heavy-sea slam detection through Jolt;
- `DeepRunW1WaveSlamHapticTests`;
- normal gameplay startup smoke;
- acoustic/windowed smoke;
- P-700 production launch smoke.

The original W1-D regression was an architecture violation: `SurfaceImpactSystem.h` indirectly pulled
`Engine/Physics` through `BuoyancySystem.h`. The accepted implementation replaces that dependency with a
Marine-owned primitive sample contract and explicitly compiles `SurfaceImpactSystem.cpp` into
`DeepRunEngine`, preventing an uncompiled implementation from appearing green again.
