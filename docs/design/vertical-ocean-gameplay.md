# DeepRun Ocean Vertical Presentation Contract

Status: Accepted design contract

Specification: D0 — Core Game Design / Game Loop specialized contract

This contract separates three things that must never be conflated:

- the normal submarine gameplay depth band;
- regional bathymetry/seabed depth;
- presentation composition for local, tactical and strategic views.

## 1. Normal / local gameplay

The primary interactive submarine gameplay band is:

```text
0 m      = sea surface
-700 m   = lower boundary of the normal submarine gameplay band
```

This range defines the normal vertical area for submarine operation, combat framing,
sensor/weapon gameplay and typical encounters. It is not an ocean-bottom constraint.

Depth is expressed positively downward from the authoritative WaterBody surface.

| Depth | Gameplay band |
|---:|---|
| `0–20 m` | Surface / periscope zone |
| `20–100 m` | Shallow / high-risk zone |
| `100–300 m` | Primary operating and combat depth |
| `300–500 m` | Deep tactical zone |
| `500–600 m` | Extreme depth for vessel classes that permit it |
| `600–700 m` | Lower boundary of the normal submarine gameplay band |

The `700 m` value does not mean that every submarine may safely reach 700 m.
Vessel-specific safe/test/emergency/crush depths remain owned by vessel/runtime contracts.

## 2. Bathymetry is regional world data

Bathymetry belongs to the region/world, not to the camera and not to the submarine gameplay band.
A seabed must never be fabricated at `-700 m` merely because the normal submarine gameplay band ends there.

Typical design ranges:

| Region | Typical seabed depth |
|---|---:|
| Shelf / shallow | `-100 … -300 m` |
| Continental / normal combat | `-250 … -700 m` |
| Deep ocean | `-700 … -2000 m` |
| Abyssal | below `-2000 m` |

These are design/environment ranges, not submarine depth limits.

## 3. Deep-water / abyss presentation

When the real/known seabed lies significantly below the normal gameplay band, or when the current M5 scene has no authoritative bathymetry outside the authored local section, presentation must not invent a false floor.

The intended state is `DEEP WATER / ABYSS`:

- no artificial seabed plane at `-700 m`;
- no giant filled slab;
- no visible blue water below a rendered seabed;
- the water column below the normal gameplay band darkens progressively;
- distant seabed may be completely absent from the frame.

The visual message to the player is deliberate: there may be kilometres of ocean below the submarine.

## 4. Normal camera priority

Normal/local gameplay prioritizes underwater combat over sky.

Baseline composition:

- above-water band: approximately `10–20%`, target about `15%`;
- underwater space dominates;
- seabed is shown only when actual/known bathymetry makes it visible;
- local terrain, flora and fauna remain readable;
- generic Arctic ice is scenario-specific rather than a default signature.

The accepted M5 normal/local horizontal span remains `800 m`.

## 5. Tactical camera priority

Tactical view has a different purpose. It must preserve the submarine tactical situation while also reserving useful airspace for future ASW aircraft, helicopters, sonobuoys and air-deployed weapons.

Tactical composition therefore shifts upward relative to local view:

- the sea surface remains the visual anchor;
- sky receives a larger share of the frame;
- baseline sky/above-water share is approximately `25–40%`;
- the underwater tactical band remains readable;
- seabed is not required to be visible.

M5 presentation ramps from the local ~15% sky share toward roughly 32% in tactical view and may rise toward 36–40% at operational/strategic scales.

This remains an aspect-correct world camera. Full production models must not be stretched merely to force a fixed vertical meter span.

## 6. Air-threat context

Future production camera logic may increase visible sky when an air threat is present or expected. Aircraft/helicopter presentation may therefore push the surface lower on screen than a quiet tactical scene.

When no air threat is present, tactical composition may allocate more space back to the ocean.

This is a context-aware presentation rule; it does not move simulation state.

## 7. Tactical seabed rule

At tactical zoom:

- known shallow/useful bathymetry may be shown using a simplified representation;
- deep or unknown bathymetry must transition to deliberate deep-water/abyss presentation instead of a false floor;
- local M3 terrain must never be wallpaper-tiled across kilometres;
- operational/strategic views should eventually use symbolic/map-style bathymetry rather than literal giant 3D terrain.

The temporary M5 strategic seabed profile is not world authority. It may be used only when a scenario explicitly opts in to known wide-area bathymetry. Generic M5 combat must not assume it.

## 8. Future procedural bathymetry

The intended future architecture is one deterministic world-space bathymetry source, potentially chunked/procedural and optionally mission-authored, feeding suitable LOD representations for:

- rendering;
- terrain/collision authority;
- navigation constraints;
- acoustic environment;
- mission generation.

M5 does not implement that full system. Current presentation code must remain replaceable by it.

## 9. Critical distinctions

```text
NORMAL SUBMARINE GAMEPLAY BAND ~= 0 … -700 m
```

does not mean:

```text
SEABED MUST BE ABOVE -700 m
WORLD ENDS AT -700 m
EVERY SUBMARINE MAY DIVE TO -700 m
```

Likewise, seeing deep water below `-700 m` in tactical presentation does not expand the allowed operating depth of the player vessel.

## 10. Intended player experience

- Shallow water: constrained space and nearby seabed are visually obvious.
- Ordinary combat water: bathymetry is a meaningful tactical part of the scene.
- Deep ocean: the submarine may have a dark, apparently bottomless ocean beneath it.

Deep water is an environment identity, not a renderer fallback.

## M5 tactical sky baseline

M5 manual acceptance uses a deterministic daytime presentation so increased tactical sky share reads as intentional airspace rather than an unfinished black clear. The production free-presentation path therefore shows a blue vertical sky gradient and a small sun cue above the authoritative sea surface. This is not a time-of-day simulation. Future weather/day-night work may replace the M5 baseline with scenario-owned sun/moon/lighting state while preserving the same surface/airspace composition contract.

## Authority boundary

Presentation must remain downstream of authoritative world/simulation state:

```text
WaterBody / regional bathymetry / vessel contracts
    -> suitable simulation, collision, navigation and acoustic representations
    -> presentation LOD / camera composition
```

No presentation rule may silently become physics, navigation, acoustic or vessel-depth authority.

## M5 applicability

For M5 closure:

- accepted `800 m` local framing remains unchanged;
- local authored M3 seabed/flora/fauna remain single-instance and non-tiled;
- generic wide view must prefer deliberate `DEEP WATER / ABYSS` when no wide-area bathymetry authority exists;
- tactical zoom progressively allocates more screen space to sky;
- the old generic strategic seabed slab must not appear merely because the camera zoomed out;
- the current M5 combat region may opt in to a render-only tactical bathymetry continuity profile whose local anchors match the accepted M3 section; unknown/deep regions still use the abyss presentation.
- M5 free-presentation uses a deterministic DAY sky baseline (scene-linear sky gradient plus sun cue); full time-of-day, weather and moon/night presentation remain future content scope.

This contract does not start P-700, M6, procedural-world generation, aircraft AI or new simulation scope.
