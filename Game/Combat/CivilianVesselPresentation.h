#pragma once

#include "Engine/Render/ModelDraw.h"
#include "Game/Combat/CombatPlaygroundRuntime.h"

#include <array>
#include <cmath>
#include <expected>
#include <string>
#include <vector>

namespace DeepRun::Game::Combat
{
// Read-only presentation boundary for the normal-play civilian participant. It consumes only a physics body
// snapshot plus damage state. No Track classification, truth label or weapon decision enters the renderer.
[[nodiscard]] inline std::expected<std::vector<Render::ModelDrawInstance>, std::string>
BuildCivilianVesselPresentationDraws(
    const CombatPlaygroundRuntime& runtime,
    const Physics::PhysicsWorld& physicsWorld)
{
    if (!runtime.Civilian())
    {
        return std::vector<Render::ModelDrawInstance>{};
    }
    const auto& civilian = *runtime.Civilian();
    const auto& definition = runtime.CivilianDefinition();
    if (!civilian.body.IsValid() || civilian.integrity.body != civilian.body ||
        civilian.definitionId != definition.id || !std::isfinite(civilian.integrity.maximumIntegrity) ||
        civilian.integrity.maximumIntegrity <= 0.0F || !std::isfinite(civilian.integrity.remainingIntegrity) ||
        civilian.integrity.remainingIntegrity < 0.0F ||
        civilian.integrity.remainingIntegrity > civilian.integrity.maximumIntegrity)
    {
        return std::unexpected("civilian presentation authority is invalid");
    }
    const auto body = physicsWorld.GetBodyState(civilian.body);
    if (!body || !body->position.IsFinite() || !body->orientation.IsFinite())
    {
        return std::unexpected("civilian presentation body snapshot is unavailable");
    }

    const auto makePose = [&](const Physics::PhysicsVector3& scale,
                              const Physics::PhysicsVector3& localOffset,
                              const std::array<float, 4>& color,
                              std::string materialName)
        -> std::expected<Render::ModelDrawInstance, std::string>
    {
        const auto& q = body->orientation;
        const double lengthSquared = static_cast<double>(q.x) * q.x + static_cast<double>(q.y) * q.y +
                                     static_cast<double>(q.z) * q.z + static_cast<double>(q.w) * q.w;
        if (!std::isfinite(lengthSquared) || lengthSquared <= 1.0e-12 || !scale.IsFinite() ||
            scale.x <= 0.0F || scale.y <= 0.0F || scale.z <= 0.0F)
        {
            return std::unexpected("civilian presentation transform input is invalid");
        }
        const double inv = 1.0 / std::sqrt(lengthSquared);
        const double x = q.x * inv;
        const double y = q.y * inv;
        const double z = q.z * inv;
        const double w = q.w * inv;
        Assets::ModelTransform bodyToWorld{};
        bodyToWorld.values[0] = static_cast<float>(1.0 - 2.0 * (y * y + z * z));
        bodyToWorld.values[1] = static_cast<float>(2.0 * (x * y + w * z));
        bodyToWorld.values[2] = static_cast<float>(2.0 * (x * z - w * y));
        bodyToWorld.values[4] = static_cast<float>(2.0 * (x * y - w * z));
        bodyToWorld.values[5] = static_cast<float>(1.0 - 2.0 * (x * x + z * z));
        bodyToWorld.values[6] = static_cast<float>(2.0 * (y * z + w * x));
        bodyToWorld.values[8] = static_cast<float>(2.0 * (x * z + w * y));
        bodyToWorld.values[9] = static_cast<float>(2.0 * (y * z - w * x));
        bodyToWorld.values[10] = static_cast<float>(1.0 - 2.0 * (x * x + y * y));
        bodyToWorld.values[12] = body->position.x;
        bodyToWorld.values[13] = body->position.y;
        bodyToWorld.values[14] = body->position.z;

        Assets::ModelTransform local{};
        local.values[0] = scale.x;
        local.values[5] = scale.y;
        local.values[10] = scale.z;
        local.values[12] = localOffset.x;
        local.values[13] = localOffset.y;
        local.values[14] = localOffset.z;
        const Assets::ModelTransform modelToWorld = Render::Multiply(bodyToWorld, local);
        const auto normalToWorld = Render::BuildNormalTransform(modelToWorld);
        if (!normalToWorld)
        {
            return std::unexpected("civilian presentation normal transform failed: " + normalToWorld.error());
        }
        return Render::ModelDrawInstance{
            .nodeIndex = 0U,
            .primitiveIndex = 0U,
            .modelToWorld = modelToWorld,
            .normalToWorld = *normalToWorld,
            .material = Assets::ModelMaterialData{
                .name = std::move(materialName),
                .baseColorFactor = color,
                .metallicFactor = 0.06F,
                .roughnessFactor = 0.72F}};
    };

    const float integrity = std::clamp(
        civilian.integrity.remainingIntegrity / civilian.integrity.maximumIntegrity, 0.0F, 1.0F);
    const float damage = 0.62F + 0.38F * integrity;
    const Physics::PhysicsVector3 fullScale{
        .x = definition.collisionHalfExtentsMeters.x * 2.0F,
        .y = definition.collisionHalfExtentsMeters.y * 2.0F,
        .z = definition.collisionHalfExtentsMeters.z * 2.0F};
    auto hull = makePose(
        fullScale,
        {},
        {0.46F * damage, 0.45F * damage, 0.41F * damage, 1.0F},
        "CivilianMerchantHull");
    auto superstructure = makePose(
        {.x = 22.0F, .y = 12.0F, .z = 9.0F},
        {.x = -17.0F, .y = 9.5F, .z = 0.0F},
        {0.76F * damage, 0.74F * damage, 0.66F * damage, 1.0F},
        "CivilianMerchantSuperstructure");
    if (!hull || !superstructure)
    {
        return std::unexpected(hull ? superstructure.error() : hull.error());
    }
    std::vector<Render::ModelDrawInstance> draws;
    draws.reserve(2U);
    draws.push_back(std::move(*hull));
    draws.push_back(std::move(*superstructure));
    return draws;
}
} // namespace DeepRun::Game::Combat
