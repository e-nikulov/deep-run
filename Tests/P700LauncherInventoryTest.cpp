#include "Game/Weapons/P700LaunchGeometry.h"
#include "Game/Weapons/P700LauncherInventory.h"
#include "Game/Weapons/PlayerWeaponSelection.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

namespace
{
[[nodiscard]] std::array<DeepRun::Game::Submarine::ProductionLaunchAnchor,
                         DeepRun::Game::Armament::AnteyP700LauncherSlotCount>
BuildAnchors()
{
    using DeepRun::Game::Submarine::ProductionLaunchAnchor;
    constexpr float pitchRadians = 0.6981317007977318F;
    std::array<ProductionLaunchAnchor, DeepRun::Game::Armament::AnteyP700LauncherSlotCount> anchors{};
    for (std::size_t index = 0; index < anchors.size(); ++index)
    {
        anchors[index].semanticId = "p700.fixture." + std::to_string(index + 1U);
        anchors[index].localTransform.values[12] = 10.0F + static_cast<float>(index);
        anchors[index].localTransform.values[13] = (index % 2U == 0U) ? 2.0F : -2.0F;
        anchors[index].localTransform.values[14] = 1.0F;
        anchors[index].launchForward = {
            .x = static_cast<float>(std::cos(static_cast<double>(pitchRadians))),
            .y = static_cast<float>(std::sin(static_cast<double>(pitchRadians))),
            .z = 0.0F};
    }
    return anchors;
}

[[nodiscard]] bool Near(const float first, const float second) noexcept
{
    return std::abs(first - second) <= 1.0e-4F;
}

[[nodiscard]] bool RunInventoryChecks()
{
    using namespace DeepRun::Game::Armament;

    const auto anchors = BuildAnchors();
    auto inventoryResult = P700LauncherInventory::Create(anchors);
    if (!inventoryResult)
    {
        std::cerr << "P-700 launcher inventory creation failed: " << inventoryResult.error() << '\n';
        return false;
    }
    P700LauncherInventory inventory = std::move(*inventoryResult);
    if (inventory.Slots().size() != AnteyP700LauncherSlotCount ||
        inventory.LoadedCount() != AnteyP700LauncherSlotCount || inventory.SpentCount() != 0U ||
        inventory.FirstLoadedSlotIndex() != 0U)
    {
        std::cerr << "P-700 initial 24-slot inventory state is invalid\n";
        return false;
    }

    const auto consumed = inventory.Consume(0U);
    if (!consumed || consumed->semanticId != anchors.front().semanticId ||
        inventory.LoadedCount() != AnteyP700LauncherSlotCount - 1U || inventory.SpentCount() != 1U ||
        inventory.FirstLoadedSlotIndex() != 1U || inventory.Consume(0U))
    {
        std::cerr << "P-700 launcher slot consumption is not one-way\n";
        return false;
    }
    if (inventory.Consume(AnteyP700LauncherSlotCount))
    {
        std::cerr << "P-700 launcher inventory accepted an out-of-range slot\n";
        return false;
    }
    for (std::size_t index = 1U; index < AnteyP700LauncherSlotCount; ++index)
    {
        if (!inventory.Consume(index))
        {
            std::cerr << "P-700 launcher inventory could not consume slot " << index << '\n';
            return false;
        }
    }
    if (inventory.LoadedCount() != 0U || inventory.SpentCount() != AnteyP700LauncherSlotCount ||
        inventory.FirstLoadedSlotIndex().has_value())
    {
        std::cerr << "P-700 exhausted inventory state is invalid\n";
        return false;
    }

    std::array<DeepRun::Game::Submarine::ProductionLaunchAnchor, 23> shortSet{};
    if (P700LauncherInventory::Create(shortSet))
    {
        std::cerr << "P-700 inventory accepted fewer than 24 anchors\n";
        return false;
    }

    auto duplicate = anchors;
    duplicate[1].semanticId = duplicate[0].semanticId;
    if (P700LauncherInventory::Create(duplicate))
    {
        std::cerr << "P-700 inventory accepted duplicate semantic IDs\n";
        return false;
    }

    auto malformed = anchors;
    malformed[0].localTransform.values[12] = std::numeric_limits<float>::quiet_NaN();
    if (P700LauncherInventory::Create(malformed))
    {
        std::cerr << "P-700 inventory accepted non-finite anchor transform\n";
        return false;
    }

    auto downward = anchors;
    downward[0].launchForward.y = -downward[0].launchForward.y;
    if (P700LauncherInventory::Create(downward))
    {
        std::cerr << "P-700 inventory accepted a downward launcher forward vector\n";
        return false;
    }
    return true;
}

[[nodiscard]] bool RunWeaponSelectorChecks()
{
    using namespace DeepRun::Game::Armament;
    if (PlayerWeaponName(PlayerWeaponType::HeavyweightTorpedo) != "USET-80" ||
        PlayerWeaponName(PlayerWeaponType::P700Granit) != "P-700 GRANIT" ||
        CyclePlayerWeapon(PlayerWeaponType::HeavyweightTorpedo, 1) != PlayerWeaponType::P700Granit ||
        CyclePlayerWeapon(PlayerWeaponType::P700Granit, 1) != PlayerWeaponType::HeavyweightTorpedo ||
        CyclePlayerWeapon(PlayerWeaponType::HeavyweightTorpedo, -1) != PlayerWeaponType::P700Granit)
    {
        std::cerr << "M5 two-item Weapon Selector cycle is invalid\n";
        return false;
    }
    return true;
}

[[nodiscard]] bool RunWorldGeometryChecks()
{
    using namespace DeepRun;
    using namespace DeepRun::Game::Armament;

    const auto anchors = BuildAnchors();
    const Physics::PhysicsBodyState body{
        .position = {.x = 100.0F, .y = -20.0F, .z = 7.0F},
        .orientation = {},
        .linearVelocity = {},
        .angularVelocity = {},
        .active = true};
    Assets::ModelTransform modelToBody{};
    modelToBody.values[12] = -5.0F;
    modelToBody.values[13] = 3.0F;
    modelToBody.values[14] = 2.0F;

    const auto forward = ComposeP700WorldLaunchAnchor(anchors.front(), body, modelToBody, 1, false);
    if (!forward || forward->semanticId != anchors.front().semanticId ||
        !Near(forward->positionMeters.x, 105.0F) || !Near(forward->positionMeters.y, -15.0F) ||
        !Near(forward->positionMeters.z, 10.0F) ||
        !Near(forward->forwardUnitVector.x, anchors.front().launchForward.x) ||
        !Near(forward->forwardUnitVector.y, anchors.front().launchForward.y) ||
        !Near(forward->forwardUnitVector.z, 0.0F))
    {
        std::cerr << "P-700 forward-facing production anchor composition is invalid\n";
        return false;
    }

    const auto reversed = ComposeP700WorldLaunchAnchor(anchors.front(), body, modelToBody, -1, false);
    if (!reversed || !Near(reversed->positionMeters.x, 95.0F) ||
        !Near(reversed->positionMeters.y, -15.0F) || !Near(reversed->positionMeters.z, 4.0F) ||
        !Near(reversed->forwardUnitVector.x, -anchors.front().launchForward.x) ||
        !Near(reversed->forwardUnitVector.y, anchors.front().launchForward.y) ||
        !Near(reversed->forwardUnitVector.z, 0.0F))
    {
        std::cerr << "P-700 reversed 2.5D-facing anchor composition is invalid\n";
        return false;
    }

    if (ComposeP700WorldLaunchAnchor(anchors.front(), body, modelToBody, 1, true) ||
        ComposeP700WorldLaunchAnchor(anchors.front(), body, modelToBody, 0, false))
    {
        std::cerr << "P-700 launch geometry did not reject turnaround/invalid-facing state\n";
        return false;
    }

    auto malformedModelToBody = modelToBody;
    malformedModelToBody.values[12] = std::numeric_limits<float>::infinity();
    if (ComposeP700WorldLaunchAnchor(anchors.front(), body, malformedModelToBody, 1, false))
    {
        std::cerr << "P-700 launch geometry accepted non-finite model-to-body correction\n";
        return false;
    }
    return true;
}
} // namespace

int main()
{
    if (!RunInventoryChecks() || !RunWeaponSelectorChecks() || !RunWorldGeometryChecks())
    {
        return EXIT_FAILURE;
    }
    std::cout << "P-700 24-slot launcher inventory/world geometry: PASS\n";
    return EXIT_SUCCESS;
}
