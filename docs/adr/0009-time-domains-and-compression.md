# ADR-0009: Time domains and simulation-time compression

## Status

Accepted

## Decision

DeepRun distinguishes `RealTime`, `PresentationTime`, and `SimulationTime`.
Authoritative gameplay advances only from fixed-step `SimulationTime`.

Player time compression advances more fixed simulation work per unit of real
time; it does not enlarge the authoritative physics step. Tactical pause sets
simulation progress to zero while permitted UI/presentation work may continue.

## Consequences

- physics, acoustics, contacts/tracks, AI, weapons, damage, flooding, crew, and
  gameplay-relevant mission timers use `SimulationTime`;
- audio-device time and render-frame time are never gameplay clocks;
- distant systems may use deterministic reduced-rate tiers where their
  contracts permit it;
- automatic slowdown policy and exact multipliers remain later design/tuning;
- headless tests can reproduce time-compressed outcomes from simulation-owned
  clocks and deterministic random sources.
