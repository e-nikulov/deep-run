#pragma once

#include "Engine/Assets/ModelAsset.h"
#include "Engine/Physics/PhysicsTypes.h"

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
// is fully runnable headless. M3-B.1 adds independent coarse collision descriptions below;
// PhysicsWorld owns their backend bodies, while this section remains environment authority.
//
// DeepRun's rule that "the renderer is not the source of truth" is preserved here: the renderer
// only consumes the ModelAsset; this section (owned by the scenario) is the source of the stable
// identity and the world-space bounds that later independent representations (coarse collision,
// navigation, acoustic terrain queries) can key against without any renderer handle leaking in.
//
// Scope for M3-B: ONE deterministic representative seabed section with a modest, authored
// (non-procedural) profile. This is architecture + a visible floor, NOT a terrain system: no
// Perlin/procedural generation, no chunk streaming/background paging/origin rebasing, no LOD, no
// navigation or acoustics. M3-B.1 adds only coarse static boxes. One bounded section is enough
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

// One ordered world-space control point of the authored side-view seabed profile. Consecutive
// knots form deliberate linear geological transitions: gentle slopes remain gentle while an
// escarpment can remain visibly sharp. This is authored data, never a noise field.
struct SeabedProfileControlPoint final
{
    float xMeters = 0.0F;
    float yMeters = 0.0F;

    bool operator==(const SeabedProfileControlPoint&) const noexcept = default;
};

// Deterministic authored seabed profile. All values are world-space (meters, DeepRun convention:
// +X horizontal / vessel-forward, +Y up, +Z toward the fixed side-view camera; 1 unit = 1 metre).
// The explicit ordered knots are the major terrain authoring model. Rendering tessellates those
// knots; coarse collision samples them independently.
struct SeabedProfileConfig final
{
    // X extent of the section (metres). Must be a finite, non-degenerate span.
    float minX = -400.0F;
    float maxX = 400.0F;

    // Number of X samples along the surface. Must be in the bounded prototype range [2, 4096].
    int sampleCount = 161;

    // How far below the surface the section body is filled (world Y, metres) so the lower part of
    // the gameplay view is covered. Must be below every authored profile knot.
    float fillBottomYMeters = -550.0F;

    // Bounded representative M3-B.2 terrain: gentle floor -> broad ridge -> sharp drop-off ->
    // flat deep trench -> recovery. End knots intentionally coincide with minX/maxX.
    std::vector<SeabedProfileControlPoint> controlPoints{
        {-400.0F, -160.0F}, {-320.0F, -155.0F}, {-250.0F, -150.0F},
        {-180.0F, -125.0F}, {-130.0F, -120.0F}, {-90.0F, -132.0F},
        {-45.0F, -140.0F}, {-20.0F, -205.0F}, {0.0F, -220.0F},
        {80.0F, -220.0F}, {120.0F, -205.0F}, {180.0F, -180.0F},
        {260.0F, -170.0F}, {400.0F, -165.0F}};

    // A small finite Z thickness (metres) gives the side-view seabed a bounded renderable cross-section
    // with stable winding rather than a zero-thickness plane. M3-B does not require a watertight closed solid.
    // Gameplay remains fundamentally side-view / XY.
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

// Stable Game-owned placement of one low-poly representative rock. position is the terrain-contact
// anchor, not a GPU handle or renderer transform. Its mesh and optional coarse collision are built
// independently from this authored value.
struct EnvironmentRockInstance final
{
    std::string id;
    Assets::ModelVector3 position{};
    Assets::ModelVector3 halfExtents{};
    float rotationRadians = 0.0F;
    bool hasCoarseCollision = false;

    bool operator==(const EnvironmentRockInstance& other) const noexcept
    {
        return id == other.id && position.x == other.position.x && position.y == other.position.y &&
               position.z == other.position.z && halfExtents.x == other.halfExtents.x &&
               halfExtents.y == other.halfExtents.y && halfExtents.z == other.halfExtents.z &&
               rotationRadians == other.rotationRadians && hasCoarseCollision == other.hasCoarseCollision;
    }
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
    // Stable authored rock records; their presentation mesh is only one consumer.
    std::vector<EnvironmentRockInstance> rocks;
    // Independent coarse terrain/rock boxes; never derived from render vertices or GPU data.
    std::vector<Physics::StaticBoxBodyCreateInfo> collisionBoxes;
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
