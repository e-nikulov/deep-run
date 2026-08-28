# DeepRun Content Specifications

This directory contains text specifications for logically complete DeepRun
content packages classified on the C axis. The canonical taxonomy and current
specification registry are defined in [../README.md](../README.md).

A future C specification may describe the models, textures, materials, UI art,
VFX, audio, and data definitions required for one coherent package. Do not
assign a C-ID to every individual PNG, DDS, WAV, or GLB asset.

Concrete authored assets and data definitions do not belong here. They belong
under the repository's canonical `Content/` root. Runtime code for asset IDs,
loading, caching, ownership, and resource management belongs under
`Engine/Assets/`.

No C-ID is assigned yet. Add C0 only when the first concrete content package is
ready to be specified; do not create speculative placeholder specifications.
