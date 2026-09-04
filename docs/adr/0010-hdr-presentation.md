# ADR-0010: Scene-linear HDR with SDR fallback

## Status

Accepted

## Decision

DeepRun's D3D12 renderer evolves toward scene-linear HDR lighting and HDR
intermediate targets, followed by explicit tone mapping and either SDR or
capability-gated HDR display output.

HDR display state is presentation-only. The classic indexed D3D12 path remains
the compatibility path and HDR does not require unrelated advanced rendering
features.

## Consequences

- M3 owns a bounded HDR/output foundation, not a complete renderer rewrite;
- SDR remains a safe fallback;
- paper white, peak luminance, UI composition, exposure, tone mapping, and
  capture encoding are explicit renderer concerns;
- HDR settings cannot alter physics, sonar, contacts, AI knowledge, weapons,
  or damage;
- detailed behaviour is defined in `docs/architecture/rendering-spec.md`.
