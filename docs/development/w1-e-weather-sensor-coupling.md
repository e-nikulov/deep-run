# W1-E — Weather / Sensor Coupling

W1-E connects the authoritative W1 `WeatherState` to existing perceived-world sensor paths without moving
weather ownership into sensors and without reopening accepted M5 targeting/authority rules.

## Authority

Normal gameplay owns one `WeatherState`. The same value produces the production ocean spectrum and a
Game-owned `WeatherSensorEnvironment` value snapshot. Render state and wave geometry are never read back to
infer weather.

The allowed flow is:

`Environment::WeatherState -> Game::WeatherSensorEnvironment -> sensor input values -> SensorObservation -> TrackManager`

Weather never publishes hostile entity identity, body handles, exact hostile transforms, or target truth.

## Couplings

- **Passive acoustics:** wind sea and rain add bounded surface-generated ambient noise. The contribution decays
  continuously with receiver depth. Both the player and the surface combatant consume the same weather profile.
- **Active acoustics:** no separate magic weather penalty is injected. Echo/reception SNR naturally consumes the
  weather-adjusted receiver ambient noise already present at the acoustic boundary.
- **Periscope / optical watch:** authoritative meteorological visibility is passed directly. Cloud/rain reduce the
  daytime gameplay light fraction and wind/waves/rain increase spray/sea-state obscuration. Existing geographic
  horizon, field-of-view, staged identification and TrackManager fusion remain authoritative.
- **RADIAN surface radar:** rain and rough-sea clutter reduce the bounded gameplay detection envelope/confidence
  and increase range uncertainty. These coefficients are game tuning, not claimed exact equipment performance.
- **ZONA / terrestrial RF / hostile ESM:** ordinary cloud and rain do not create a generic terrestrial RF penalty.
  Lightning is the explicit W1-E broadband-interference input and can lower evidence confidence.
- **SELENA satellite reports:** rain and lightning can lower report confidence and increase position uncertainty.
  Reports remain stale perceived evidence rather than live world truth.

## Acceptance gates

W1-E passes only if:

1. calm weather is behaviorally neutral relative to the accepted baseline;
2. rough-sea passive noise is strongest near the surface and decays with depth;
3. weather affects both sides of passive acoustic detection rather than only the player;
4. fog/poor visibility and sea obscuration feed the existing periscope condition contract;
5. rain/rough sea degrade the surface-radar evidence envelope without changing radar authority;
6. rain alone does not reduce generic terrestrial RF/ESM confidence, while lightning may;
7. satellite-report weather effects modify uncertainty/confidence only, never truth identity;
8. all sensor evidence still crosses `SensorObservation`/`TrackManager` boundaries;
9. Debug and Release CI, all CTest targets, normal startup, acoustic smoke and P-700 smoke remain green.

No damage, crew injury, flooding, weather graphics, lightning rendering/audio, or M6 system-health resolution is
introduced by W1-E.
