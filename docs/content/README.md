# DeepRun Content Specifications

This directory contains text specifications for logically complete DeepRun
content packages classified on the C axis. The canonical taxonomy and current
specification registry are defined in [../README.md](../README.md).

A C specification describes the models, textures, materials, UI art, VFX,
audio, and data definitions required for one coherent package. Do not assign a
C-ID to every individual PNG, DDS, WAV, or GLB asset.

Concrete editable/source assets and data definitions belong under the
repository's canonical `Content/` root. Runtime-ready assets belong in
asset-specific subdirectories under `Engine/Assets/`, alongside the runtime
asset code already owned by that directory. The C0 source-to-runtime procedure
is defined in [asset-pipeline.md](asset-pipeline.md).

## Registry

| ID | Package | Specification | Status |
|---|---|---|---|
| C0 | Player Submarine package: prototype + Antey/P700 production assets | [submarine-prototype.md](submarine-prototype.md); [antey-asset.md](antey-asset.md); [p700-asset.md](p700-asset.md) | Technically validated; human art approval and legal shipping review pending |
