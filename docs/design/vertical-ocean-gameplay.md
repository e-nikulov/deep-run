# DeepRun Vertical Ocean Gameplay Contract

Status: Accepted design contract

Specification: D0 — Core Game Design / Game Loop specialized contract

The project-wide M/A/D/C taxonomy and specification registry are defined in
`docs/README.md`. This contract refines D0 for the vertical scale of normal
underwater gameplay and presentation. It does not define vessel-specific crush
or test depths and does not grant every submarine the same depth capability.

## Purpose

Deep Run is an underwater game. Normal gameplay must keep the playable and
readable ocean volume bounded enough that submarines, seabed, terrain, flora,
fauna, weapons and the water surface can be composed as one coherent scene.

The canonical design reference uses:

```text
0 m      = sea surface
-700 m   = lower boundary of the normal visible/playable ocean
```

The target maximum vertical scale of normal gameplay is therefore approximately
`700 m`.

Runtime systems may express the same contract relative to the authoritative
`WaterBody` surface level rather than assuming that every scenario stores the
surface at world-space Y=0. The gameplay depth interval remains 0..700 m below
that authoritative surface.

## Depth bands

Depth is expressed positively downward from the authoritative sea surface.

| Depth | Gameplay band |
|---:|---|
| `0–20 m` | Surface / periscope zone |
| `20–100 m` | Shallow / high-risk zone |
| `100–300 m` | Primary operating and combat depth |
| `300–500 m` | Deep tactical zone |
| `500–600 m` | Extreme depth for vessel classes that permit it |
| `600–700 m` | Lower world-boundary region: seabed, relief, canyons and the visual limit of the playable ocean |

These bands describe world/gameplay composition. They are not universal vessel
limits.

## Environment contract

In normal gameplay the visible seabed top surface must remain inside the
vertical ocean domain from the sea surface down to approximately `-700 m`
relative to the canonical reference surface.

Seabed depth may vary by mission and authored region. Typical design ranges are:

| Region | Typical seabed depth |
|---|---:|
| Shelf / shallow water | `100–250 m` |
| Ordinary combat region | `250–450 m` |
| Deep-water region | `450–650 m` |
| Lower authored-environment boundary | approximately `700 m` |

The normal gameplay environment must not require rendering kilometres of water
column merely to keep distant tactical actors visible.

Bathymetry may contain shelves, ridges, cliffs, trenches, canyons and other
local relief as long as the normal-play presentation remains inside this
vertical contract. Terrain below approximately 700 m is outside normal gameplay
presentation unless a later explicitly authored mode or scenario changes this
contract.

A render-only skirt or hidden continuation used to prevent a visible mesh
underside may extend below the gameplay boundary. Such geometry must never be
interpreted as playable seabed, navigation authority, collision authority,
acoustic terrain authority or an expansion of the normal gameplay depth range.

## Camera contract

Normal/local combat composition must be designed around the bounded vertical
ocean scale.

It must:

- prioritize underwater readability;
- keep the surface as a small upper band rather than splitting the view roughly
  50/50 between air and water;
- keep seabed and local relief in the lower portion of the scene when the
  authored region contains them;
- keep the player submarine and nearby gameplay objects human-readable;
- avoid showing kilometres of water column solely to preserve tactical
  visibility.

A tactical or strategic overview may cover a much larger horizontal area using
presentation LOD, contextual framing, symbols or simplified environment
composition. Increasing tactical horizontal coverage does not redefine the
normal gameplay vertical ocean as a multi-kilometre water column.

## Simulation boundary

`700 m` is a gameplay/world-presentation boundary.

It is not a statement that every submarine may dive to 700 m.

Vessel-specific safe depth, test depth, emergency depth, structural limits,
damage effects and any crush-depth gameplay belong to the vessel/runtime
contract for that submarine class.

The required separation is:

```text
WORLD VISIBLE / NORMAL GAMEPLAY DEPTH ~= 700 m
```

and not:

```text
EVERY SUBMARINE MAY DIVE TO 700 m
```

No renderer, camera or environment-presentation rule may silently override a
vessel-specific simulation limit. Likewise, a vessel limit must not force the
whole world presentation to expose a deeper water column than normal gameplay
requires.

## Authority boundary

This D0 contract defines player-facing world scale and composition.

It does not make render geometry authoritative. The existing architecture still
applies:

```text
WaterBody / authored world / vessel contracts
    -> suitable simulation, collision, navigation and acoustic representations
    -> presentation LOD / camera composition
```

Future deterministic/chunked bathymetry may provide multiple representations
from one terrain authority. That future system should preserve this normal-play
vertical contract unless the design contract is explicitly revised.

## M5 applicability

M5-V1.2 uses this contract immediately for visual composition:

- normal/local combat remains a genuinely local underwater view;
- the strategic seabed top silhouette must stay inside the normal ocean depth
  domain;
- hidden strategic-seabed extrusion may continue below `-700 m` only as
  non-authoritative render fill;
- wide/tactical presentation must not be mistaken for the normal gameplay
  vertical scale.

This contract does not expand M5 simulation scope and does not add new terrain,
physics, navigation, acoustic, weapon or AI authority.
