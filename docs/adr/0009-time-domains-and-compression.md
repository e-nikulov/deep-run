# ADR-0009: Time domains and simulation-time compression

## Status

Accepted / baseline runtime implemented

## Decision

DeepRun distinguishes `RealTime`, `PresentationTime`, and `SimulationTime`.
Authoritative gameplay advances only from fixed-step `SimulationTime`.

Player time compression advances more fixed simulation work per unit of real
time; it does not enlarge the authoritative physics step. Tactical pause is a
separate future command/UI feature and is not implemented by setting the current
compression controller to zero.

The implemented baseline exposes authored rates `1x`, `2x`, `4x`, and `8x`.
Keyboard `-` / `+` and controller D-pad Down / Up decrease or increase the
requested rate. The engine owns both a requested rate and a gameplay-settable
maximum rate; the effective rate is the lower of the two. This allows future
incoming-weapon, collision, casualty, flooding, fire, or other safety policy to
clamp accelerated time without destroying the player's prior intent.

Explicit high-consequence player actions currently break requested compression
to `1x` for weapon fire and defensive decoy deployment. Broader automatic
slowdown policy remains gameplay-owned tuning and should use the engine maximum
rate API rather than modifying fixed-step duration.

## Consequences

- physics, acoustics, contacts/tracks, AI, weapons, damage, flooding, crew, and
  gameplay-relevant mission timers use `SimulationTime`;
- audio-device time and render-frame time are never gameplay clocks;
- the authoritative fixed step remains unchanged under compression; only the
  number of fixed ticks accumulated from a RealTime frame increases;
- distant systems may use deterministic reduced-rate tiers where their
  contracts permit it;
- `PresentationTime`, audio-device timing, and haptic ageing are not globally
  pitch/speed-scaled by time compression;
- tactical pause remains separate because zero fixed ticks would otherwise
  prevent the current fixed-update command path from consuming new orders;
- headless tests can reproduce time-compressed outcomes from simulation-owned
  clocks and deterministic random sources.
