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


## M5 sky/sun and SDR precision refinement

The daytime tactical sky and sun remain presentation-only. Their continuous gradient, anti-aliased solar disk,
and atmospheric aureole are evaluated in scene-linear FP16 before the existing output transfer, so SDR and HDR
consume the same radiance source. Game supplies only the projected sky/waterline presentation mask; it does not
create lighting, weather, physics, sensor or gameplay authority.

For SDR, the renderer now prefers `R10G10B10A2_UNORM` only when the matched `IDXGIOutput6` reports at least
10 bits per colour and the created swap chain confirms `RGB_FULL_G22_NONE_P709` Present support. Otherwise it
retains the established `R8G8B8A8_UNORM` path. Each path dithers at its own quantization step (1023 or 255).
HDR remains FP16 scRGB and keeps 10-bit-aware output dithering for common compositor/scan-out quantization.
