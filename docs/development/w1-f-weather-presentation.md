# W1-F — Weather Presentation

## Scope

W1-F makes the existing authoritative `WeatherState` visible without creating a second weather authority in Render.
Game maps the same state already consumed by the W1 ocean and W1-E sensor coupling into a small renderer-neutral
presentation snapshot. Render never includes or queries `WeatherState`.

## Production path

`WeatherState -> Game::EvaluateWeatherPresentation -> Render::ScenePresentationParameters -> shared b1 frame snapshot`

The shared GPU frame snapshot is 128 bytes (8 float4 registers) and is consumed by opaque model presentation,
underwater particles (layout parity only), and the final scene-linear output pass. The output pass adds procedural
cloud cover, horizon haze, sun attenuation and rain presentation without a new render target or a volumetric-cloud draw.

Cloud/rain animation uses renderer presentation time only. `SimulationTime` remains the authoritative phase source for
physical ocean waves and is not replaced or inferred by W1-F.

## Authority boundaries

- Meteorological visibility, cloud cover, rain, wind direction/speed and the weather seed originate in `WeatherState`.
- The Game mapper produces presentation coefficients only; none are written back to Simulation, Physics, Acoustics,
  Perception or Combat.
- W1-E remains the gameplay/sensor authority for periscope, passive sonar, radar, RF/ESM and satellite effects.
- Above-surface model extinction is only a finite camera-ray visual veil for geometry already submitted to Render; it
  does not decide detection, identification, target validity or weapon permission.
- Rain is clipped to the screen-space region above the projected sea surface and is not drawn underwater.
- Legacy M2/M3 fixed benchmark presentation uses neutral atmosphere defaults. W1-F is enabled by the free gameplay
  presentation path, preserving accepted historical visual regression contracts.

## Visual model

- Cloud coverage is an analytic four-octave deterministic field seeded from `weatherSeed`.
- Cloud drift is presentation-only and follows the signed world-X projection of the authored wind direction.
- Sun disk/corona remains scene-linear and is attenuated by cloud/rain transmittance before SDR/HDR output mapping.
- Horizon haze trends toward a weather-derived atmosphere colour as visibility falls.
- Rain uses a deterministic procedural streak field plus bounded near-horizon mist; no particle simulation authority is
  introduced.

## Acceptance gates

W1-F passes only when Debug and Release CI both succeed through build, all CTest suites, normal gameplay startup,
windowed acoustic smoke, P-700 production smoke and artifact retention. Unit checks must cover calm/storm mapping,
visibility extinction, rain/cloud normalization, wind-direction advection, deterministic seed variation and invalid
presentation values.

Lightning flashes/thunder/audio are deliberately outside this stage and remain separate later weather/audio scope.
