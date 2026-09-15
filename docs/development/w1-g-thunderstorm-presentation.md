# W1-G — Thunderstorm Presentation

## Scope

W1-G extends the accepted W1 weather presentation with deterministic lightning and a semantic thunder cue while
preserving the existing authority boundaries. The exact `WeatherState` that already drives the production ocean,
weather-sensitive sensors and W1-F atmosphere remains the only weather authority.

This stage is presentation-only. It does not introduce lightning damage, electrical failures, flooding, crew injury,
mission hazards or any other M6 damage-control behavior.

## Production path

`WeatherState -> Game::EvaluateThunderstormPresentation -> Render::ScenePresentationParameters -> shared b1 frame snapshot`

The strike schedule is deterministic from `weatherSeed`, `lightningRatePerMinute` and PresentationTime. Render receives
only normalized optical values: flash intensity, strike screen position and a stable pattern offset. It never receives
`WeatherState` or thunder/audio semantics.

W1-G appends one float4 register to the existing frame-local scene-presentation payload, increasing it from 128 to
144 bytes while retaining the same 256-byte D3D12 constant-buffer allocation. No new render target, geometry pass or
lightning draw call is introduced; the existing scene-linear output pass renders the flash and anti-aliased channel.
Opaque models and underwater particles retain layout parity with the shared `b1` payload.

## Lightning presentation

- Strike ordering and shape are deterministic for a given weather seed.
- Bounded strike-interval jitter prevents metronomic flashes while preserving monotonic strike ordering.
- The optical event uses a short multi-pulse flash envelope rather than a single full-screen white frame.
- The visible channel has one narrow anti-aliased primary path plus a restrained branch.
- The cloud deck and above-water scene receive a cold transient illumination around the strike azimuth.
- Flash and bolt are clipped to the projected sea-surface boundary, so W1-G never paints lightning into the underwater
  presentation region.
- SDR8, SDR10 and HDR scRGB consume the same scene-linear lightning source before their existing output mapping.

## Thunder cue contract

Game derives a deterministic presentation distance in the bounded 1.2–9.0 km range. The semantic thunder cue is due
after `distance / 343 m/s`, keeping the visible flash effectively immediate while delaying the audible event by the
speed-of-sound travel time. Gain and rumble duration are bounded Game presentation values.

`ThunderstormCueTracker` owns only presentation one-shot bookkeeping. Rewinding PresentationTime clears pending cues,
so stale thunder is not replayed after a reset or time discontinuity.

The current `AudioEngine` exposes initialization/status only and has no generic one-shot/event submission API. W1-G
therefore deliberately stops at the tested `ThunderAudioCue` boundary instead of leaking weather semantics into
Engine/Audio or adding an ad-hoc miniaudio path. A later audio slice may map this semantic cue into a generic Engine
one-shot request behind the existing miniaudio abstraction.

## Time and authority boundaries

- Lightning scheduling and flash animation use PresentationTime only and never replace `SimulationTime`.
- `SimulationTime` remains authoritative for the physical ocean and gameplay simulation.
- W1-G does not change W1-E sensor/perception visibility, target validity, weapon permission or acoustic propagation.
- Render remains a passive consumer of finite Game-derived values and has no dependency on Simulation weather types.
- M6 remains the sole future owner of damage, crew and flooding consequences.

## Acceptance gates

W1-G passes only when Debug and Release CI both succeed through shader/C++ build, all CTest suites, normal gameplay
startup, windowed acoustic smoke, P-700 production launch smoke and artifact retention. Regression checks cover calm
weather, deterministic strike identity, flash decay, bounded bolt parameters, exact light-to-sound delay, delayed
one-shot cue emission, time-rewind reset behavior and renderer validation of all lightning frame values.
