#pragma once

#include "Engine/Assets/ModelAsset.h"
#include "Engine/Render/ModelDraw.h"
#include "Game/Combat/CombatPlaygroundRuntime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace DeepRun::Game::Combat
{
// M5-H.1-A read-only boundary between live combat simulation and presentation. No TrackManager, weapon target,
// body handle, damage command, or mutable runtime object crosses into the renderer-facing draw builder.
struct CombatPlaygroundTorpedoPresentation final
{
    Physics::PhysicsVector3 positionMeters{};
    float headingRadians = 0.0F;
    Weapons::MovementDomain movementDomain = Weapons::MovementDomain::Attached;
};

struct CombatPlaygroundDecoyPresentation final
{
    Physics::PhysicsVector3 positionMeters{};
    bool active = false;
};

struct CombatPlaygroundExplosionPresentation final
{
    Physics::PhysicsVector3 positionMeters{};
    float radiusMeters = 0.0F;
    float normalizedAge = 0.0F;
};

struct CombatPlaygroundPresentationSnapshot final
{
    Physics::PhysicsBodyState destroyerBody{};
    float destroyerIntegrityFraction = 1.0F;
    bool destroyerDestroyed = false;
    std::optional<CombatPlaygroundTorpedoPresentation> playerTorpedo{};
    std::optional<CombatPlaygroundDecoyPresentation> decoy{};
    std::optional<CombatPlaygroundExplosionPresentation> explosion{};
};

enum class CombatPlaygroundPresentationElement
{
    DestroyerHull,
    DestroyerSuperstructure,
    PlayerTorpedo,
    AcousticDecoy,
    Explosion,
};

struct CombatPlaygroundPresentationDraw final
{
    CombatPlaygroundPresentationElement element = CombatPlaygroundPresentationElement::DestroyerHull;
    Render::ModelDrawInstance draw{};
};

inline constexpr double M5CombatExplosionPresentationLifetimeSeconds = 1.0;

[[nodiscard]] inline std::expected<CombatPlaygroundPresentationSnapshot, std::string>
BuildCombatPlaygroundPresentationSnapshot(
    const CombatPlaygroundRuntime& runtime,
    const Physics::PhysicsWorld& physicsWorld,
    const double simulationTimeSeconds)
{
    if (!physicsWorld.IsInitialized() || !std::isfinite(simulationTimeSeconds) || simulationTimeSeconds < 0.0)
    {
        return std::unexpected("M5-H.1 presentation snapshot input is invalid");
    }

    const auto& destroyer = runtime.Destroyer();
    const auto& destroyerDefinition = runtime.DestroyerDefinition();
    if (!destroyer.body.IsValid() || destroyer.integrity.body != destroyer.body ||
        !std::isfinite(destroyer.integrity.maximumIntegrity) || destroyer.integrity.maximumIntegrity <= 0.0F ||
        !std::isfinite(destroyer.integrity.remainingIntegrity) || destroyer.integrity.remainingIntegrity < 0.0F ||
        destroyer.integrity.remainingIntegrity > destroyer.integrity.maximumIntegrity ||
        destroyer.definitionId != destroyerDefinition.id)
    {
        return std::unexpected("M5-H.1 destroyer presentation authority is invalid");
    }

    const auto destroyerBody = physicsWorld.GetBodyState(destroyer.body);
    if (!destroyerBody || !destroyerBody->position.IsFinite() || !destroyerBody->orientation.IsFinite())
    {
        return std::unexpected("M5-H.1 destroyer body snapshot is unavailable");
    }

    CombatPlaygroundPresentationSnapshot snapshot{
        .destroyerBody = *destroyerBody,
        .destroyerIntegrityFraction = std::clamp(
            destroyer.integrity.remainingIntegrity / destroyer.integrity.maximumIntegrity, 0.0F, 1.0F),
        .destroyerDestroyed = destroyer.integrity.destroyed};

    if (const auto& torpedo = runtime.PlayerTorpedo(); torpedo.has_value())
    {
        if (!torpedo->positionMeters.IsFinite() || !std::isfinite(torpedo->headingRadians))
        {
            return std::unexpected("M5-H.1 torpedo presentation state is invalid");
        }
        snapshot.playerTorpedo = CombatPlaygroundTorpedoPresentation{
            .positionMeters = torpedo->positionMeters,
            .headingRadians = torpedo->headingRadians,
            .movementDomain = torpedo->movementDomain};
    }

    if (const auto& decoy = runtime.Decoy(); decoy.has_value())
    {
        if (!decoy->emitter.positionMeters.IsFinite())
        {
            return std::unexpected("M5-H.1 decoy presentation state is invalid");
        }
        snapshot.decoy = CombatPlaygroundDecoyPresentation{
            .positionMeters = decoy->emitter.positionMeters,
            .active = decoy->active};
    }

    if (const auto& explosion = runtime.LastExplosion(); explosion.has_value())
    {
        if (!explosion->positionMeters.IsFinite() || !std::isfinite(explosion->radiusMeters) ||
            explosion->radiusMeters <= 0.0F || !std::isfinite(explosion->simulationTimeSeconds) ||
            simulationTimeSeconds < explosion->simulationTimeSeconds)
        {
            return std::unexpected("M5-H.1 explosion presentation state is invalid or time-reversing");
        }
        const double ageSeconds = simulationTimeSeconds - explosion->simulationTimeSeconds;
        if (ageSeconds <= M5CombatExplosionPresentationLifetimeSeconds)
        {
            snapshot.explosion = CombatPlaygroundExplosionPresentation{
                .positionMeters = explosion->positionMeters,
                .radiusMeters = explosion->radiusMeters,
                .normalizedAge = static_cast<float>(
                    std::clamp(ageSeconds / M5CombatExplosionPresentationLifetimeSeconds, 0.0, 1.0))};
        }
    }

    return snapshot;
}

[[nodiscard]] inline std::expected<Assets::ModelAsset, std::string> BuildCombatPlaygroundPresentationModel()
{
    const auto assetId = Assets::AssetId::FromPath("combat/m5-playground-presentation.model");
    if (!assetId)
    {
        return std::unexpected("M5-H.1 presentation model could not create a stable asset id: " + assetId.error());
    }

    Assets::ModelAsset model{.id = *assetId};
    model.materials.push_back(Assets::ModelMaterialData{
        .name = "M5CombatProxy",
        .baseColorFactor = {0.32F, 0.38F, 0.40F, 1.0F},
        .metallicFactor = 0.15F,
        .roughnessFactor = 0.78F});

    Assets::MeshPrimitiveData primitive;
    primitive.vertices.reserve(24U);
    primitive.indices.reserve(36U);

    const auto addFace = [&primitive](
        const Assets::ModelVector3& a,
        const Assets::ModelVector3& b,
        const Assets::ModelVector3& c,
        const Assets::ModelVector3& d,
        const Assets::ModelVector3& normal)
    {
        const std::uint32_t base = static_cast<std::uint32_t>(primitive.vertices.size());
        primitive.vertices.push_back({.position = a, .normal = normal});
        primitive.vertices.push_back({.position = b, .normal = normal});
        primitive.vertices.push_back({.position = c, .normal = normal});
        primitive.vertices.push_back({.position = d, .normal = normal});
        primitive.indices.insert(primitive.indices.end(), {base, base + 1U, base + 2U, base, base + 2U, base + 3U});
    };

    constexpr float h = 0.5F;
    addFace({h, -h, -h}, {h, h, -h}, {h, h, h}, {h, -h, h}, {1.0F, 0.0F, 0.0F});
    addFace({-h, -h, h}, {-h, h, h}, {-h, h, -h}, {-h, -h, -h}, {-1.0F, 0.0F, 0.0F});
    addFace({-h, h, -h}, {-h, h, h}, {h, h, h}, {h, h, -h}, {0.0F, 1.0F, 0.0F});
    addFace({-h, -h, h}, {-h, -h, -h}, {h, -h, -h}, {h, -h, h}, {0.0F, -1.0F, 0.0F});
    addFace({-h, -h, h}, {h, -h, h}, {h, h, h}, {-h, h, h}, {0.0F, 0.0F, 1.0F});
    addFace({h, -h, -h}, {-h, -h, -h}, {-h, h, -h}, {h, h, -h}, {0.0F, 0.0F, -1.0F});

    primitive.materialIndex = 0U;
    primitive.localBounds = {.minimum = {-h, -h, -h}, .maximum = {h, h, h}};
    primitive.hasNormals = true;
    model.primitives.push_back(std::move(primitive));
    model.nodes.push_back(Assets::MeshNodeData{
        .name = "M5CombatProxyCube",
        .localToModel = {},
        .primitiveIndices = {0U}});
    model.bounds = {.minimum = {-h, -h, -h}, .maximum = {h, h, h}};
    return model;
}

namespace CombatPlaygroundPresentationDetail
{
[[nodiscard]] inline Assets::ModelTransform LocalScaleTranslation(
    const Physics::PhysicsVector3& scale,
    const Physics::PhysicsVector3& translation) noexcept
{
    Assets::ModelTransform transform{};
    transform.values[0] = scale.x;
    transform.values[5] = scale.y;
    transform.values[10] = scale.z;
    transform.values[12] = translation.x;
    transform.values[13] = translation.y;
    transform.values[14] = translation.z;
    return transform;
}

[[nodiscard]] inline std::expected<Assets::ModelTransform, std::string> BodyPoseTransform(
    const Physics::PhysicsVector3& position,
    const Physics::PhysicsQuaternion& orientation)
{
    if (!position.IsFinite() || !orientation.IsFinite())
    {
        return std::unexpected("M5-H.1 presentation pose must be finite");
    }
    const double lengthSquared = static_cast<double>(orientation.x) * orientation.x +
                                 static_cast<double>(orientation.y) * orientation.y +
                                 static_cast<double>(orientation.z) * orientation.z +
                                 static_cast<double>(orientation.w) * orientation.w;
    if (!std::isfinite(lengthSquared) || lengthSquared <= 1.0e-12)
    {
        return std::unexpected("M5-H.1 presentation orientation must be non-zero");
    }

    const double inverseLength = 1.0 / std::sqrt(lengthSquared);
    const double qx = orientation.x * inverseLength;
    const double qy = orientation.y * inverseLength;
    const double qz = orientation.z * inverseLength;
    const double qw = orientation.w * inverseLength;

    Assets::ModelTransform transform{};
    transform.values[0] = static_cast<float>(1.0 - 2.0 * (qy * qy + qz * qz));
    transform.values[1] = static_cast<float>(2.0 * (qx * qy + qw * qz));
    transform.values[2] = static_cast<float>(2.0 * (qx * qz - qw * qy));
    transform.values[4] = static_cast<float>(2.0 * (qx * qy - qw * qz));
    transform.values[5] = static_cast<float>(1.0 - 2.0 * (qx * qx + qz * qz));
    transform.values[6] = static_cast<float>(2.0 * (qy * qz + qw * qx));
    transform.values[8] = static_cast<float>(2.0 * (qx * qz + qw * qy));
    transform.values[9] = static_cast<float>(2.0 * (qy * qz - qw * qx));
    transform.values[10] = static_cast<float>(1.0 - 2.0 * (qx * qx + qy * qy));
    transform.values[12] = position.x;
    transform.values[13] = position.y;
    transform.values[14] = position.z;

    for (const float value : transform.values)
    {
        if (!std::isfinite(value))
        {
            return std::unexpected("M5-H.1 presentation pose produced non-finite transform");
        }
    }
    return transform;
}

[[nodiscard]] inline std::expected<Assets::ModelTransform, std::string> PoseScaleTransform(
    const Physics::PhysicsVector3& position,
    const Physics::PhysicsQuaternion& orientation,
    const Physics::PhysicsVector3& scale,
    const Physics::PhysicsVector3& localOffset = {})
{
    if (!position.IsFinite() || !orientation.IsFinite() || !scale.IsFinite() || !localOffset.IsFinite() ||
        scale.x <= 0.0F || scale.y <= 0.0F || scale.z <= 0.0F)
    {
        return std::unexpected("M5-H.1 presentation transform input is invalid");
    }
    const auto bodyToWorld = BodyPoseTransform(position, orientation);
    if (!bodyToWorld)
    {
        return std::unexpected(bodyToWorld.error());
    }
    return Render::Multiply(*bodyToWorld, LocalScaleTranslation(scale, localOffset));
}

[[nodiscard]] inline Assets::ModelMaterialData Material(
    std::string name,
    const std::array<float, 4>& baseColor,
    const float metallic,
    const float roughness)
{
    return Assets::ModelMaterialData{
        .name = std::move(name),
        .baseColorFactor = baseColor,
        .metallicFactor = metallic,
        .roughnessFactor = roughness};
}

[[nodiscard]] inline std::expected<CombatPlaygroundPresentationDraw, std::string> MakeDraw(
    const CombatPlaygroundPresentationElement element,
    const Assets::ModelTransform& transform,
    Assets::ModelMaterialData material)
{
    const auto normal = Render::BuildNormalTransform(transform);
    if (!normal)
    {
        return std::unexpected("M5-H.1 presentation normal transform failed: " + normal.error());
    }
    return CombatPlaygroundPresentationDraw{
        .element = element,
        .draw = Render::ModelDrawInstance{
            .nodeIndex = 0U,
            .primitiveIndex = 0U,
            .modelToWorld = transform,
            .normalToWorld = *normal,
            .material = std::move(material)}};
}
}

[[nodiscard]] inline std::expected<std::vector<CombatPlaygroundPresentationDraw>, std::string>
BuildCombatPlaygroundPresentationDraws(const CombatPlaygroundPresentationSnapshot& snapshot)
{
    using namespace CombatPlaygroundPresentationDetail;
    if (!snapshot.destroyerBody.position.IsFinite() || !snapshot.destroyerBody.orientation.IsFinite() ||
        !std::isfinite(snapshot.destroyerIntegrityFraction) || snapshot.destroyerIntegrityFraction < 0.0F ||
        snapshot.destroyerIntegrityFraction > 1.0F)
    {
        return std::unexpected("M5-H.1 presentation snapshot is invalid");
    }

    std::vector<CombatPlaygroundPresentationDraw> draws;
    draws.reserve(5U);

    const auto hullTransform = PoseScaleTransform(
        snapshot.destroyerBody.position,
        snapshot.destroyerBody.orientation,
        {.x = 50.0F, .y = 6.0F, .z = 6.0F});
    const auto superstructureTransform = PoseScaleTransform(
        snapshot.destroyerBody.position,
        snapshot.destroyerBody.orientation,
        {.x = 14.0F, .y = 3.0F, .z = 4.0F},
        {.x = -2.0F, .y = 4.0F, .z = 0.0F});
    if (!hullTransform || !superstructureTransform)
    {
        return std::unexpected("M5-H.1 destroyer presentation transform failed");
    }

    const float damageDarkening = 0.55F + 0.45F * snapshot.destroyerIntegrityFraction;
    auto hull = MakeDraw(
        CombatPlaygroundPresentationElement::DestroyerHull,
        *hullTransform,
        Material("M5DestroyerHull", {0.24F * damageDarkening, 0.31F * damageDarkening,
                                      0.33F * damageDarkening, 1.0F}, 0.24F, 0.72F));
    auto superstructure = MakeDraw(
        CombatPlaygroundPresentationElement::DestroyerSuperstructure,
        *superstructureTransform,
        Material("M5DestroyerSuperstructure", {0.32F * damageDarkening, 0.38F * damageDarkening,
                                                0.40F * damageDarkening, 1.0F}, 0.18F, 0.76F));
    if (!hull || !superstructure)
    {
        return std::unexpected("M5-H.1 destroyer presentation draw failed");
    }
    draws.push_back(std::move(*hull));
    draws.push_back(std::move(*superstructure));

    if (snapshot.playerTorpedo && snapshot.playerTorpedo->movementDomain == Weapons::MovementDomain::Underwater)
    {
        const auto transform = PoseScaleTransform(
            snapshot.playerTorpedo->positionMeters,
            Weapons::WeaponHeadingQuaternion(snapshot.playerTorpedo->headingRadians),
            {.x = 4.0F, .y = 0.65F, .z = 0.65F});
        if (!transform)
        {
            return std::unexpected(transform.error());
        }
        auto draw = MakeDraw(
            CombatPlaygroundPresentationElement::PlayerTorpedo,
            *transform,
            Material("M5PlayerTorpedo", {0.52F, 0.55F, 0.56F, 1.0F}, 0.36F, 0.48F));
        if (!draw)
        {
            return std::unexpected(draw.error());
        }
        draws.push_back(std::move(*draw));
    }

    if (snapshot.decoy && snapshot.decoy->active)
    {
        const auto transform = PoseScaleTransform(
            snapshot.decoy->positionMeters,
            {},
            {.x = 2.0F, .y = 2.0F, .z = 2.0F});
        if (!transform)
        {
            return std::unexpected(transform.error());
        }
        auto draw = MakeDraw(
            CombatPlaygroundPresentationElement::AcousticDecoy,
            *transform,
            Material("M5AcousticDecoy", {0.18F, 0.68F, 0.76F, 1.0F}, 0.08F, 0.42F));
        if (!draw)
        {
            return std::unexpected(draw.error());
        }
        draws.push_back(std::move(*draw));
    }

    if (snapshot.explosion)
    {
        const float pulseScale = snapshot.explosion->radiusMeters *
            (0.55F + 0.45F * (1.0F - snapshot.explosion->normalizedAge));
        const auto transform = PoseScaleTransform(
            snapshot.explosion->positionMeters,
            {},
            {.x = pulseScale * 2.0F, .y = pulseScale * 2.0F, .z = pulseScale * 2.0F});
        if (!transform)
        {
            return std::unexpected(transform.error());
        }
        auto draw = MakeDraw(
            CombatPlaygroundPresentationElement::Explosion,
            *transform,
            Material("M5ImpactExplosion", {1.0F, 0.46F, 0.12F, 1.0F}, 0.0F, 0.28F));
        if (!draw)
        {
            return std::unexpected(draw.error());
        }
        draws.push_back(std::move(*draw));
    }

    return draws;
}
} // namespace DeepRun::Game::Combat
