# Third-Party Notices Registry

This registry records dependencies currently fetched by
`cmake/Dependencies.cmake`. It is not a substitute for shipping the complete
license texts required by each dependency. Before distribution, generate the
shipping notice bundle from the exact resolved dependency versions and retain
their complete upstream license files.

| Component | Pinned version | Upstream | Recorded upstream licence |
|---|---|---|---|
| Jolt Physics | `v5.5.0` | <https://github.com/jrouwe/JoltPhysics> | MIT; copyright 2021 Jorrit Rouwe |
| miniaudio | `0.11.23` | <https://github.com/mackron/miniaudio> | Upstream offers a choice of public-domain/Unlicense or MIT No Attribution; project choice not yet recorded |
| Dear ImGui | `v1.91.9b` | <https://github.com/ocornut/imgui> | MIT; copyright 2014-2025 Omar Cornut |
| EnTT | `v3.16.0` | <https://github.com/skypjack/entt> | MIT; copyright 2017-2025 Michele Caini |
| nlohmann/json | `v3.12.0` | <https://github.com/nlohmann/json> | MIT; copyright 2013-2025 Niels Lohmann |
| fastgltf | `v0.9.0` | <https://github.com/spnda/fastgltf> | MIT; copyright 2022-2025 Sean Apeler |

The licence labels and copyright lines above were checked against the license
files in the locally fetched pinned source trees. They are summaries only.

Direct3D 12, DXGI, DXC, Windows SDK, Windows.Gaming.Input, and related Microsoft
platform components are used through the applicable installed SDK/toolchain;
their redistribution terms must be evaluated for the actual shipping package.

Third-party content is tracked separately in
`docs/content/asset-provenance.md` and per-asset reference manifests. Any asset
marked `NON-SHIPPABLE UNTIL RESOLVED` must be excluded from a shipping package
until its rights and obligations are verified.
