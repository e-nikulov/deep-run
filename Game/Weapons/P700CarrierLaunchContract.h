#pragma once

#include "Game/Submarine/AnteyPhysicalCollisionProxy.h"
#include "Game/Weapons/P700LaunchGeometry.h"
#include "Game/Weapons/P700LauncherInventory.h"

#include <cmath>
#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace DeepRun::Game::Weapons
{
// Immutable carrier-side geometry paired with a separately owned mutable launcher inventory.
// The collision center is the same production model->body pivot correction already accepted by
// PhysicalPlayground; the live body/facing state arrives each tick through the read-only physical proxy.
class P700CarrierLaunchContract final
{
public:
    [[nodiscard]] static std::expected<P700CarrierLaunchContract, std::string> Create(
        const std::span<const Submarine::ProductionLaunchAnchor> anchors,
        const Assets::ModelVector3& productionCollisionLocalCenter)
    {
        const auto inventoryValidation = P700LauncherInventory::Create(anchors);
        if (!inventoryValidation)
        {
            return std::unexpected("P-700 carrier launch contract rejected launcher inventory: " +
                                   inventoryValidation.error());
        }
        if (!std::isfinite(productionCollisionLocalCenter.x) || !std::isfinite(productionCollisionLocalCenter.y) ||
            !std::isfinite(productionCollisionLocalCenter.z))
        {
            return std::unexpected("P-700 carrier production collision center must be finite");
        }

        Assets::ModelTransform modelToBody{};
        modelToBody.values[12] = -productionCollisionLocalCenter.x;
        modelToBody.values[13] = -productionCollisionLocalCenter.y;
        modelToBody.values[14] = -productionCollisionLocalCenter.z;
        return P700CarrierLaunchContract(
            std::vector<Submarine::ProductionLaunchAnchor>(anchors.begin(), anchors.end()), modelToBody);
    }

    [[nodiscard]] std::expected<P700LauncherInventory, std::string> CreateInventory() const
    {
        return P700LauncherInventory::Create(anchors_);
    }

    [[nodiscard]] std::expected<P700WorldLaunchAnchor, std::string> BuildWorldAnchor(
        const std::size_t slotIndex,
        const Submarine::AnteyPhysicalCollisionProxySnapshot& playerProxy) const
    {
        if (slotIndex >= anchors_.size())
        {
            return std::unexpected("P-700 carrier launch slot index is out of range");
        }
        if (!playerProxy.body.IsValid() || !playerProxy.positionMeters.IsFinite() ||
            !playerProxy.orientation.IsFinite() ||
            (playerProxy.gameplayLongitudinalFacingSign != 1.0F &&
             playerProxy.gameplayLongitudinalFacingSign != -1.0F))
        {
            return std::unexpected("P-700 carrier physical proxy is invalid");
        }

        const Physics::PhysicsBodyState bodyState{
            .position = playerProxy.positionMeters,
            .orientation = playerProxy.orientation,
            .linearVelocity = {},
            .angularVelocity = {},
            .active = true};
        const int facingSign = playerProxy.gameplayLongitudinalFacingSign > 0.0F ? 1 : -1;
        return ComposeP700WorldLaunchAnchor(
            anchors_[slotIndex], bodyState, modelToBody_, facingSign, playerProxy.turningAround);
    }

    [[nodiscard]] std::span<const Submarine::ProductionLaunchAnchor> Anchors() const noexcept
    {
        return anchors_;
    }

private:
    P700CarrierLaunchContract(
        std::vector<Submarine::ProductionLaunchAnchor> anchors,
        Assets::ModelTransform modelToBody)
        : anchors_(std::move(anchors)), modelToBody_(modelToBody)
    {
    }

    std::vector<Submarine::ProductionLaunchAnchor> anchors_;
    Assets::ModelTransform modelToBody_{};
};
} // namespace DeepRun::Game::Weapons
