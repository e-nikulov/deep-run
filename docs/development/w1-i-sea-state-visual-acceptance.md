# W1-I — Sea-State Visual Acceptance

## Scope

W1-I adds a bounded visual-review harness for the production W1 ocean. It does not add a second weather model,
second wave generator or alternate renderer. The exact production path remains:

`WeatherState -> BuildProductionOceanWaveField -> WaterBody -> BuildGerstnerSurfacePresentation -> D3D12`

The harness exists only to capture deterministic comparison frames for human visual review.

## Review presets

The acceptance set is Beaufort 0, 1, 2 and 8 using `WeatherState::FullyDevelopedBeaufort` with one stable seed and
travel direction. This gives a directly comparable progression from flat/calm water through light sea to a gale-scale
production sea. Beaufort 0 deliberately exercises the valid no-wave `WaterBody` contract: an empty spectral optional
means flat water and is not an error.

The acceptance environment variable `DR_WEATHER_VISUAL_BEAUFORT=0..12` is parsed only by the executable composition
layer. Normal gameplay keeps the existing mixed Beaufort-5 wind-sea plus oblique swell scenario unchanged.

## Capture contract

- The existing Win32 `WindowFrameCapture` path is reused; there is no screenshot-only renderer.
- The game runs the ordinary D3D12 production scene and advances authoritative fixed-step `SimulationTime`.
- Capture occurs after at least 2 seconds of SimulationTime and a renderer warm-up, so the image is a settled live frame.
- The acceptance camera uses the existing local gameplay framing and suppresses combat composition only to keep the
  ocean silhouette and production Antey readable.
- The submarine starts 3 m below mean sea level so the surface and near-surface vessel reference remain visible without
  modifying buoyancy or wave authority.
- Files are named `w1-sea-beaufort-<force>.bmp` and retained as a GitHub Actions artifact.

## Authority boundaries

The environment variable selects an existing `WeatherState` preset; it never supplies wave amplitudes, periods or
render coefficients directly. Physical ocean phase still comes exclusively from `SimulationTime`. W1-F/G presentation
continues to consume the same `WeatherState`. No sensor, weapon, damage, flooding or crew authority is introduced.

## Acceptance

Automated gates require the ordinary Debug and Release CI chain to remain green. The W1-I capture job additionally
requires all four requested images (0, 1, 2 and 8) to be produced by a Release build. Subjective image quality is
intentionally not reduced to a pixel threshold: final visual acceptance belongs to human review of the retained frames.
