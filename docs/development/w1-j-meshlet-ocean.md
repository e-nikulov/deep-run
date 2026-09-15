# W1-J — Meshlet Ocean Surface / Smooth Silhouette

## Goal

Remove visible polygonal stepping from the W1 production ocean without introducing hardware tessellation,
a second wave model, CPU wave simulation, per-frame mesh rebuilding, or four prebuilt indexed LOD meshes.

The authority path remains exactly:

`WeatherState -> ProductionOceanSpectrum -> WaterBody -> Gerstner presentation -> D3D12`

Only the renderer-owned geometry submission changes.

## Production geometry path

On adapters exposing `D3D12_FEATURE_D3D12_OPTIONS7::MeshShaderTier` and Shader Model 6.5, the renderer uses
a procedural mesh-shader path. `GerstnerSurfaceMS.cso` receives the same seven-wave draw constants as the
compatibility vertex shader. No persistent ocean vertex or index buffer is allocated.

One meshlet covers at most 31 horizontal cells. It emits at most 64 vertices and 62 triangles. Shared boundary
samples are duplicated between adjacent meshlets so no meshlet references another meshlet's output.

At the 2560 px project target a rough sea selects 4096 horizontal cells and dispatches 133 meshlets in one
`DispatchMesh(133, 1, 1)` call. Wave phase still uses absolute world X, so camera movement never owns wave state.

## Compatibility path

Adapters without Mesh Shader Tier support, WARP, or environments that cannot expose
`ID3D12GraphicsCommandList6` retain the existing indexed Gerstner path. This is a compatibility fallback only;
it is not a second ocean model and does not change simulation or weather authority.

## Density policy

The procedural cell budget is bounded to 256..8192 cells. Flat water uses the minimum. Active waves combine a
screen-space target with spectral evidence for wavelengths that project to a visible vertical amplitude. Rough
water targets approximately 0.625 px per horizontal cell; sub-pixel spectral components cannot force max density.

No vertex/index buffer is rebuilt per frame. Geometry density is transported only as draw constants and mesh
workgroup count.

## Acceptance gates

- clean Debug and Release configure/build/test;
- focused W1-J regression validates 256/4096/8192-cell budgets and exact meshlet/output counts;
- mesh-capable hardware logs the mesh shader path and keeps persistent ocean VB/IB at zero;
- unsupported hardware reaches the indexed compatibility path without renderer failure;
- one ocean submission per frame on either path;
- B0/B1/B2/B8 production captures remain successful;
- B8 no longer exposes obvious triangular/polyline stepping at the production target resolution;
- no HS/DS hardware tessellation and no parallel wave authority are introduced.
