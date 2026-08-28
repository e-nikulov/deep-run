# C0 - Player Submarine Prototype

Status: Implemented for Milestone 2 content bootstrap

## Purpose

C0 supplies the minimal technical submarine mesh required to verify M2 scale,
orientation, pivot, rendering, material transport, and later
physics/controller integration. It is a prototype/debug asset, not production
art and not a model of Project 949A Antey.

## Package contents

- one approximately `100 m` long, `11 m` diameter hull;
- one simple sail/conning tower;
- one combined low-complexity control-surfaces mesh with stern planes and rudders;
- one separate low-poly five-blade technical propeller mesh;
- one PBR material with base colour, roughness, and metallic values;
- one reproducible Blender generator;
- one editable `.blend` source and one runtime `.glb` output.

Object names:

```text
SM_Submarine_Prototype_Hull
SM_Submarine_Prototype_Sail
SM_Submarine_Prototype_ControlSurfaces
SM_Submarine_Prototype_Propeller
```

Material name:

```text
M_Submarine_Prototype
```

The propeller remains a separate mesh/node. Its object origin is at the hub
centre and its local rotation axis is `+X`, allowing a future presentation-only
rotation without changing the authoritative propulsion state. C0 contains no
propeller animation, RPM logic, propulsion simulation, or propeller physics.

## Explicit exclusions

C0 contains no weapons, launchers, realistic propeller geometry, antennas, hatches,
decals, interior, production UV/textures, LODs, animation, damage states, VFX,
ocean, or seabed content.

Generation and coordinate details are canonical in
[asset-pipeline.md](asset-pipeline.md).
