# W1-J — Adaptive Sea Surface Tessellation / Smooth Silhouette

## Goal

Remove visible polygonal stepping from the ocean silhouette without introducing a second ocean model, CPU wave simulation, per-frame mesh rebuilding, or a wasteful always-max tessellation policy.

The production authority path remains unchanged:

`WeatherState -> ProductionOceanSpectrum -> WaterBody -> Gerstner presentation -> D3D12`

Only the immutable presentation mesh density is improved.

## GPU geometry policy

The renderer prebuilds four small immutable Gerstner surface LODs once:

- 513 horizontal samples
- 1025 horizontal samples
- 2049 horizontal samples
- 4097 horizontal samples

Each horizontal sample still has one surface vertex and one bottom-fill vertex, so the maximum LOD contains 8194 vertices and 8192 triangles (24576 indices). This is intentionally tiny relative to the normal Deep Run scene budget.

The renderer keeps one draw call. It selects the cheapest available LOD per frame from screen-space and spectral evidence; no vertex/index buffers are rebuilt during gameplay.

## Selection policy

Flat water uses the 513-sample LOD.

For active waves, the policy combines:

1. target horizontal vertex spacing in screen pixels (about 1.0 px for very small seas, 0.75 px for moderate amplitude, and 0.60 px for rough seas), and
2. a minimum number of samples across a wavelength only when that component projects to at least 0.25 px vertically and is therefore actually visible.

The selected requirement is rounded up to the next immutable LOD. This prevents tiny sub-pixel ripples from forcing the maximum mesh while keeping visible wave curvature smooth.

At the project target of 2560 px horizontal resolution, a rough sea selects the 4097-sample LOD. The existing 1028 px visual-acceptance viewport selects 2049 samples for the rough reference case.

## Acceptance gates

Automated regression requires:

- Beaufort 0 / flat water selects 513 samples;
- a rough 600 m camera view at 1028 px selects 2049 samples;
- the same rough view at 2560 px selects 4097 samples;
- the maximum LOD remains exactly 8194 vertices / 8192 triangles;
- invalid viewport/camera dimensions are rejected;
- Debug and Release CI, startup, acoustic smoke, and P-700 smoke remain green;
- production B0/B1/B2/B8 captures remain successful and the sea silhouette no longer shows obvious segment stepping.

W1-J does not add breaking-wave geometry, foam, spray, whitecaps, reflections, refraction, or a new weather authority. Those belong to subsequent sea-surface presentation slices.
