#pragma once

#include "Game/Submarine/ProductionAnteyAsset.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace DeepRun::Game::Armament
{
inline constexpr std::size_t AnteyP700LauncherSlotCount = 24U;

enum class P700LauncherSlotState
{
    Loaded,
    Spent,
};

struct P700LauncherSlot final
{
    Submarine::ProductionLaunchAnchor anchor{};
    P700LauncherSlotState state = P700LauncherSlotState::Loaded;
};

// Game-owned inventory layered directly on the accepted production Antey launch-anchor contract.
// It owns only load/consume state; geometry stays in ProductionLaunchAnchor and missile flight stays in
// DeepRun::Weapons. No renderer node, hostile entity or target Transform enters this boundary.
class P700LauncherInventory final
{
public:
    [[nodiscard]] static std::expected<P700LauncherInventory, std::string> Create(
        const std::span<const Submarine::ProductionLaunchAnchor> anchors)
    {
        if (anchors.size() != AnteyP700LauncherSlotCount)
        {
            return std::unexpected("P-700 launcher inventory requires exactly 24 production Antey anchors");
        }

        std::unordered_set<std::string> semanticIds;
        std::vector<P700LauncherSlot> slots;
        semanticIds.reserve(anchors.size());
        slots.reserve(anchors.size());
        for (const Submarine::ProductionLaunchAnchor& anchor : anchors)
        {
            if (anchor.semanticId.empty() || !anchor.semanticId.starts_with("p700.") ||
                !semanticIds.insert(anchor.semanticId).second)
            {
                return std::unexpected("P-700 launcher anchors must have unique non-empty p700.* semantic IDs");
            }
            if (!IsFiniteAffine(anchor.localTransform))
            {
                return std::unexpected("P-700 launcher anchor transform must be finite and affine");
            }
            const auto directionLength = DirectionLength(anchor.launchForward);
            if (!directionLength.has_value() || std::abs(*directionLength - 1.0F) > 1.0e-3F ||
                anchor.launchForward.y <= 0.0F)
            {
                return std::unexpected(
                    "P-700 launcher forward vector must be finite, normalized and upward in runtime space");
            }
            slots.push_back(P700LauncherSlot{.anchor = anchor, .state = P700LauncherSlotState::Loaded});
        }
        return P700LauncherInventory(std::move(slots));
    }

    [[nodiscard]] std::span<const P700LauncherSlot> Slots() const noexcept { return slots_; }
    [[nodiscard]] std::size_t LoadedCount() const noexcept
    {
        return static_cast<std::size_t>(std::count_if(slots_.begin(), slots_.end(), [](const auto& slot) {
            return slot.state == P700LauncherSlotState::Loaded;
        }));
    }
    [[nodiscard]] std::size_t SpentCount() const noexcept { return slots_.size() - LoadedCount(); }

    [[nodiscard]] std::optional<std::size_t> FirstLoadedSlotIndex() const noexcept
    {
        for (std::size_t index = 0; index < slots_.size(); ++index)
        {
            if (slots_[index].state == P700LauncherSlotState::Loaded)
            {
                return index;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::expected<std::vector<std::size_t>, std::string> LoadedSlotIndices(
        const std::size_t requestedCount) const
    {
        if (requestedCount == 0U || requestedCount > AnteyP700LauncherSlotCount)
        {
            return std::unexpected("P-700 salvo requested an invalid launcher count");
        }
        if (requestedCount == 1U)
        {
            const auto first = FirstLoadedSlotIndex();
            if (!first)
                return std::unexpected("P-700 launcher inventory is exhausted");
            return std::vector<std::size_t>{*first};
        }
        if (requestedCount == 2U)
        {
            // The production Antey contract is 24 missiles under 12 paired hatch groups. A Pair salvo uses
            // the two still-loaded anchors under one real authored hatch instead of inventing another lid.
            for (std::size_t first = 0U; first < slots_.size(); ++first)
            {
                if (slots_[first].state != P700LauncherSlotState::Loaded ||
                    slots_[first].anchor.hatchGroupSemanticId.empty())
                    continue;
                for (std::size_t second = first + 1U; second < slots_.size(); ++second)
                {
                    if (slots_[second].state == P700LauncherSlotState::Loaded &&
                        slots_[second].anchor.hatchGroupSemanticId == slots_[first].anchor.hatchGroupSemanticId)
                    {
                        return std::vector<std::size_t>{first, second};
                    }
                }
            }
            return std::unexpected("P-700 launcher inventory has no complete loaded paired hatch group");
        }

        std::vector<std::size_t> result;
        result.reserve(requestedCount);
        for (std::size_t index = 0; index < slots_.size() && result.size() < requestedCount; ++index)
        {
            if (slots_[index].state == P700LauncherSlotState::Loaded)
                result.push_back(index);
        }
        if (result.size() != requestedCount)
            return std::unexpected("P-700 launcher inventory does not contain enough loaded missiles for the salvo");
        return result;
    }

    // Consumption is explicit and one-way. Choosing the slot is intentionally separate so a later launch
    // policy can balance port/starboard or honor hatch-group constraints without changing inventory authority.
    [[nodiscard]] std::expected<Submarine::ProductionLaunchAnchor, std::string> Consume(
        const std::size_t slotIndex)
    {
        if (slotIndex >= slots_.size())
        {
            return std::unexpected("P-700 launcher slot index is out of range");
        }
        P700LauncherSlot& slot = slots_[slotIndex];
        if (slot.state != P700LauncherSlotState::Loaded)
        {
            return std::unexpected("P-700 launcher slot is already spent");
        }
        slot.state = P700LauncherSlotState::Spent;
        return slot.anchor;
    }

    // Salvo consumption is transactional: every requested slot is validated before any Loaded -> Spent mutation.
    // A failed pair launch therefore cannot silently expend the first missile and leave the second uncommitted.
    [[nodiscard]] std::expected<std::vector<Submarine::ProductionLaunchAnchor>, std::string> ConsumeMany(
        const std::span<const std::size_t> slotIndices)
    {
        if (slotIndices.empty() || slotIndices.size() > AnteyP700LauncherSlotCount)
        {
            return std::unexpected("P-700 salvo consumption requires one to 24 launcher slots");
        }
        std::unordered_set<std::size_t> uniqueIndices;
        uniqueIndices.reserve(slotIndices.size());
        for (const std::size_t slotIndex : slotIndices)
        {
            if (slotIndex >= slots_.size())
            {
                return std::unexpected("P-700 salvo launcher slot index is out of range");
            }
            if (!uniqueIndices.insert(slotIndex).second)
            {
                return std::unexpected("P-700 salvo launcher slot list contains a duplicate");
            }
            if (slots_[slotIndex].state != P700LauncherSlotState::Loaded)
            {
                return std::unexpected("P-700 salvo contains an already-spent launcher slot");
            }
        }

        std::vector<Submarine::ProductionLaunchAnchor> consumed;
        consumed.reserve(slotIndices.size());
        for (const std::size_t slotIndex : slotIndices)
        {
            slots_[slotIndex].state = P700LauncherSlotState::Spent;
            consumed.push_back(slots_[slotIndex].anchor);
        }
        return consumed;
    }

private:
    explicit P700LauncherInventory(std::vector<P700LauncherSlot> slots)
        : slots_(std::move(slots))
    {
    }

    [[nodiscard]] static bool IsFiniteAffine(const Assets::ModelTransform& transform) noexcept
    {
        for (const float value : transform.values)
        {
            if (!std::isfinite(value))
            {
                return false;
            }
        }
        constexpr float tolerance = 1.0e-5F;
        return std::abs(transform.values[3]) <= tolerance &&
               std::abs(transform.values[7]) <= tolerance &&
               std::abs(transform.values[11]) <= tolerance &&
               std::abs(transform.values[15] - 1.0F) <= tolerance;
    }

    [[nodiscard]] static std::optional<float> DirectionLength(const Assets::ModelVector3& direction) noexcept
    {
        if (!std::isfinite(direction.x) || !std::isfinite(direction.y) || !std::isfinite(direction.z))
        {
            return std::nullopt;
        }
        const float length = std::sqrt(
            direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
        if (!std::isfinite(length) || length <= 1.0e-6F)
        {
            return std::nullopt;
        }
        return length;
    }

    std::vector<P700LauncherSlot> slots_;
};
} // namespace DeepRun::Game::Armament
