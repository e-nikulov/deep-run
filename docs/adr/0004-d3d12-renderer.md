# ADR-0004: Direct3D 12 Renderer

## Status

Accepted

## Decision

DeepRun Engine will use Direct3D 12 as its rendering API.

The engine will not implement a generic multi-API rendering abstraction.

## Reasons

- Windows is the initial development platform
- Xbox Series X/S is a future target
- reduced engine scope
- direct control over the renderer
- HLSL and DXC provide the intended shader toolchain

## Consequences

Do not add Vulkan, OpenGL, Metal, or another graphics API unless the project requirements change significantly.

<!-- deeprun-d3d12-modernization-2026:start -->

## 2026 renderer evolution refinement

The D3D12 decision remains accepted.

DeepRun will evolve the renderer through capability-based paths rather than replace D3D12
with a generic graphics abstraction:

```text
classic indexed VS/PS
    -> GPU visibility + ExecuteIndirect where useful
    -> meshlet / mesh-shader path on supported hardware
```

The classic indexed path is the implementation path for M2 and remains the compatibility
fallback.

Modern features are capability queried. Vendor detection is not a rendering architecture.

Use pinned retail Agility SDK / DXC releases in production. Preview Agility SDK, Shader Model
preview features and experimental graphics/ML APIs are evaluation-only until promoted to retail
and justified by a DeepRun benchmark.

A later DeepRun-specific Render Graph may own pass dependencies, resource state transitions
and transient lifetimes. This does not introduce Vulkan/Metal support or a generic RHI.

The content contract remains source-model independent from the selected runtime render path:
Blender/GLB authoring must not fork into separate classic and mesh-shader assets.

<!-- deeprun-d3d12-modernization-2026:end -->
