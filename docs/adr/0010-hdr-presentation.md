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
- M3-A.1 treats `IDXGIOutput6::GetDesc1` reporting
  `RGB_FULL_G2084_NONE_P2020` with an appropriate bit depth as evidence that
  the current Windows output path is HDR-active; it does not claim that the
  physical panel itself "is PQ";
- M3-A.1 presents linear FP16 scRGB through an
  `R16G16B16A16_FLOAT` swap chain tagged
  `RGB_FULL_G10_NONE_P709`. Windows/DWM owns the required Advanced Color
  composition/output conversion toward the active HDR display path. DeepRun
  does not assume or own the final physical-link/display encoding;
- paper white, peak luminance, UI composition, exposure, tone mapping, and
  capture encoding are explicit renderer concerns;
- HDR settings cannot alter physics, sonar, contacts, AI knowledge, weapons,
  or damage;
- detailed behaviour is defined in `docs/architecture/rendering-spec.md`.
