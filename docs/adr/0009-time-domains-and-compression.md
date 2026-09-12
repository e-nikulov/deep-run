# ADR-0009: Time domains and simulation-time compression

## Status

Accepted / runtime implemented

## Decision

DeepRun distinguishes `RealTime`, `PresentationTime`, and `SimulationTime`.
Authoritative gameplay advances only from fixed-step `SimulationTime`.

Player time compression advances more fixed simulation work per unit of real
time; it does not enlarge the authoritative physics step. Tactical pause is a
separate future command/UI feature and is not implemented by setting the current
compression controller to zero.

The implemented runtime exposes authored rates `1x`, `2x`, `4x`, and `8x`.
Keyboard `-` / `+` and controller D-pad Down / Up decrease or increase the
requested rate. The engine owns both a requested rate and a gameplay-authored
maximum rate; the effective rate is the lower of the two. Safety clamps preserve
the player's requested rate so acceleration can resume automatically after the
hazard clears.

Safety caps are re-authored from authoritative gameplay state on every fixed
tick. Multiple producers may only tighten the current tick's ceiling; a later
less-urgent producer cannot accidentally relax an earlier stricter decision.
If a fixed tick discovers a stricter cap while the current render frame already
contains a precomputed accelerated packet, the engine stops executing the
remaining accelerated ticks immediately rather than carrying obsolete fast-time
work through the newly detected danger.

The current M5 combat policy is:

- `8x`: no perceived combat contact or other active M5 danger signal;
- `4x`: a non-lost perceived contact exists, or a launched P-700 is in cruise;
- `2x`: the selected Track currently qualifies a firing solution, or the player's
  conventional torpedo is in flight;
- `1x`: incoming acoustic weapon threat, player destruction, an important impact
  or mine event, P-700 hatch/underwater/water-exit/post-exit/deployment phases,
  or P-700 terminal/defeat/impact phases.

Weapon-fire and defensive-decoy input also apply an immediate one-frame `1x`
safety ceiling before the gameplay fixed tick has produced its richer state. This
is a ceiling only; it no longer destroys the player's prior requested rate.

Future M6 casualty systems such as rapid flooding, major fire, critical-depth
state and authoritative collision danger must publish through the same safety
channel once those states actually exist. They must not be approximated from UI
or invented ahead of their simulation contracts.

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
- normal direct/headless gameplay tests do not require an Engine instance: the
  safety publisher is active only inside the real Engine fixed-update scope;
- tactical pause remains separate because zero fixed ticks would otherwise
  prevent the current fixed-update command path from consuming new orders;
- headless tests can reproduce time-compressed outcomes from simulation-owned
  clocks and deterministic random sources.
