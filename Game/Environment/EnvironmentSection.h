#pragma once

#include "Engine/Assets/ModelAsset.h"

#include <expected>
#include <string>
#include <vector>

namespace DeepRun::Game
{
// M3-B — Environment geometry foundation.
//
// Architectural owner: Game (the concrete scenario) is the authoritative owner of environment
// data. This module is renderer-neutral: it produces stable identity, deterministic world-space
// bounds, and a renderer geometry payload (an Assets::ModelAsset) that the classic indexed D3D12
// path later uploads. It owns NO D3D12 resources and NO physics/navigation/acoustic state, so it
// is fully runnable headless and never becomes the presentation or collision authority.
//
// DeepRun's rule that "the renderer is not the source of truth" is preserved here: the renderer
// only consumes the ModelAsset; this section (owned by the scenario) is the source of the stable
// identity and the world-space bounds that later independent representations (coarse collision,
// navigation, acoustic terrain queries) can key against without any renderer handle leaking in.
//
// Scope for M3-B: ONE deterministic representative seabed section with a modest, authored
// (non-procedural) profile. This is architecture + a visible floor, NOT a terrain system: no
// Perlin/procedural generation, no chunk streaming/background paging/origin rebasing, no LOD, no
// collision, no navigation, no acoustics. A bounded small set of sections (here, one) is enough
// to prove chunk-compatible identity and bounds.

// Stable, renderer-independent identity for one environment section. It is a small authored
// string (not a GPU handle or an index into any runtime table), so it stays valid when the
// geometry is rebuilt/re-uploaded and never changes with the renderer or frame.
struct EnvironmentSectionId final
{
    std::string value;

    [[nodiscard]] bool IsValid() const noexcept;
    bool operator==(const EnvironmentSectionId&) const noexcept = default;
};

// Deterministic authored seabed profile. All values are world-space (meters, DeepRun convention:
// +X horizontal / vessel-forward, +Y up, +Z toward the fixed side-view camera; 1 unit = 1 metre).
// The profile is a fixed combination of two sinusoidal depth terms over an explicit X range —
// a deliberate non-flat silhouette, never procedural/infinite.
struct SeabedProfileConfig final
{
    // X extent of the section (metres). Must be a finite, non-degenerate span.
    float minX = -400.0F;
    float maxX = 400.0F;

    // Number of X samples along the surface. Must be in the bounded prototype range [2, 4096].
    int sampleCount = 161;

    // Baseline seabed depth (world Y, metres, below 0). The surface undulates about this level.
    float baselineYMeters = -150.0F;

    // How far below the surface the section body is filled (world Y, metres) so the lower part of
    // the gameplay view is covered. Must be below the baseline.
    float fillBottomYMeters = -550.0F;

    // Authored surface modulation amplitudes (metres) plus the two fixed wave periods. These are
    // content values for this section (not a noise field); the profile is fully reproducible.
    float primaryAmplitudeMeters = 14.0F;
    float secondaryAmplitudeMeters = 5.0F;
    float primaryWavePeriodMeters = 400.0F;
    float secondaryWavePeriodMeters = 150.0F;

    // A small Z thickness (metres) so the section is a bounded, closed world volume with stable
    // winding rather than a single zero-area plane. Gameplay remains fundamentally side-view / XY.
    float zThicknessMeters = 6.0F;
};

// World-space AABB of one section. Chunk-compatible: it is deterministic, contains the generated
// render geometry, and is independent of any GPU handle.
struct EnvironmentBounds final
{
    Assets::ModelVector3 minimum{};
    Assets::ModelVector3 maximum{};

    [[nodiscard]] bool IsFiniteAndValid() const noexcept;
};

// Deterministic render representation + stable identity + world bounds for one section. The
// renderGeometry is an indexed Assets::ModelAsset authored directly in WORLD space: its single
// node uses an identity localToModel, so ModelDraw::PrepareModelDraws yields a modelToWorld ==
// identity draw and the existing classic indexed D3D12 path uploads/draws it unchanged. All
// vertex colors are expected downstream of the accepted scene-linear HDR pipeline (the material
// baseColor is a linear value; the renderer applies the one SDR/HDR encode).
// If a later bounded multi-section/streaming design needs local coordinates, it can introduce a
// section transform at this composition boundary; M3-B does not create that system prematurely.
struct EnvironmentSection final
{
    EnvironmentSectionId id{};
    EnvironmentBounds bounds{};
    Assets::ModelAsset renderGeometry;
};

// Pure contract check for the canonical seabed material: the authored base color must be a finite,
// non-negative scene-linear value (the renderer adds the one presentation encode; SDR-encoded
// values must not be authored directly into the scene-linear color).
[[nodiscard]] bool IsSceneLinearBaseColor(const Assets::ModelMaterialData& material) noexcept;

// Deterministically builds the first representative seabed section from an authored profile.
// Fully pure and renderer-neutral (no D3D12, no filesystem): safe to call headless and in unit
// tests. The result is stable — the same config always produces the same section (id, bounds,
// vertex/index counts, and vertex data). Malformed configuration is a recoverable error, never
// silently corrected.
[[nodiscard]] std::expected<EnvironmentSection, std::string> BuildSeabedSection(
    const EnvironmentSectionId& id,
    const SeabedProfileConfig& profile);
} // namespace DeepRun::Game
