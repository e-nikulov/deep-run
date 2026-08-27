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