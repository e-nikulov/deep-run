# W1-K — Beaufort weather coupling

## Goal

A mission/visual-acceptance override that specifies only Beaufort force must produce one coherent maritime scene instead of changing wave height while leaving the atmosphere at clear-sky defaults.

The authority path remains:

`WeatherState -> ProductionOceanSpectrum / Game weather presentation -> WaterBody / renderer-neutral presentation -> D3D12`

No renderer-side weather authority is introduced.

## Public sea-state basis versus Game policy

The sustained wind speeds and fully developed open-sea probable wave heights in `WeatherState::FullyDevelopedBeaufort()` follow the Met Office Beaufort table. The official table also notes that these values describe well-developed wind waves and that sea response lags wind/fetch changes.

Beaufort does **not** prescribe cloud cover, rain or lightning. Deep Run therefore labels the atmospheric progression below as **GAME POLICY**. It is the default used when a mission gives only a Beaufort number. Missions may still author dry gales, fog, tropical squalls, independent swell or any other supported combination with `WeatherState::Create()`.

## Default visual progression

| Bft | Sea read | Default atmosphere/readability |
| ---: | --- | --- |
| 0 | glassy, no whitecaps | clear, no precipitation, full sun, 100 km visibility |
| 1 | tiny ripples | nearly clear; no precipitation |
| 2 | short smooth wavelets | mostly clear; no breaking crest effect |
| 3 | crests start to break | light scattered cloud; first rare white horses |
| 4 | small waves; frequent white horses | partly cloudy, still normally dry |
| 5 | pronounced moderate waves; many white horses | broken cloud; first light showers possible |
| 6 | rough sea; extensive foam crests | mostly cloudy, stronger gusts, reduced visibility |
| 7 | sea heaps up; foam begins blowing in streaks | near-gale overcast, rain, occasional distant lightning; first spindrift cue |
| 8 | 5.5 m probable / 7.5 m probable max; crest edges break into spindrift | dark ~92% cloud deck, visible rain, sun effectively occluded, 15 km default visibility, lightning/thunder active |
| 9 | high waves; dense foam streaks, rolling/toppling crests | severe overcast/rain, denser spindrift and spray, 10 km default visibility |
| 10 | very high waves; sea increasingly white, heavy tumbling | storm deck, heavy rain/spray, 6 km default visibility |
| 11 | exceptionally high; long white foam patches | violent-storm atmosphere, strong spray veil, 3.5 km default visibility |
| 12 | phenomenal; air/sea visually dominated by foam and driving spray | fully overcast hurricane presentation, extreme rain/spray, 2 km default visibility |

The whitecap/spindrift/spray values are presentation-only. They never alter wave authority, buoyancy, sensors or damage.

## Presentation policy

`EvaluateWeatherPresentation()` now:

- maps heavy rain to a visibly strong precipitation fraction before 50 mm/h so B8 rain is actually readable;
- uses nonlinear cloud occlusion for direct sunlight, preventing a bright solar disk from shining through a B8/B10 overcast deck;
- publishes deterministic Beaufort whitecap, spindrift and spray fractions for the surface/weather visual layers;
- retains exact authored visibility/rain values for sensor coupling; visual scaling does not feed back into W1-E.

## Acceptance gates

- B0 remains fully calm/clear with no rain, lightning, whitecaps, spindrift or spray;
- B8 is overcast, rainy, materially darker than B0, has strongly suppressed direct sun and non-zero lightning;
- B10 is not visually milder than B8;
- sustained wind/wave values remain the accepted Beaufort values;
- atmosphere/gust defaults are monotonic across B0..B12;
- custom `WeatherState::Create()` remains authoritative and can override the default Beaufort atmosphere completely;
- Engine/Render receives only renderer-neutral presentation values and never owns Beaufort/weather semantics.
