#include "Game/Weapons/P700LauncherInventory.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

namespace
{
[[nodiscard]] std::array<DeepRun::Game::Submarine::ProductionLaunchAnchor,
                         DeepRun::Game::Weapons::AnteyP700LauncherSlotCount>
BuildAnchors()
{
    using DeepRun::Game::Submarine::ProductionLaunchAnchor;
    constexpr float pitchRadians = 0.6981317007977318F;
    std::array<ProductionLaunchAnchor, DeepRun::Game::Weapons::AnteyP700LauncherSlotCount> anchors{};
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

[[nodiscard]] bool RunChecks()
{
    using namespace DeepRun::Game::Weapons;

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
} // namespace

int main()
{
    if (!RunChecks())
    {
        return EXIT_FAILURE;
    }
    std::cout << "P-700 24-slot launcher inventory: PASS\n";
    return EXIT_SUCCESS;
}
