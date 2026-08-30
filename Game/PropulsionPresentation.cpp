#include "Game/PropulsionPresentation.h"

#include <cmath>

namespace DeepRun::Game
{
namespace
{
constexpr double TwoPiRadians = 6.28318530717958647692;

bool IsFiniteAffine(const Assets::ModelTransform& transform) noexcept
{
    for (const float value : transform.values)
    {
        if (!std::isfinite(value))
        {
            return false;
        }
    }
    return transform.values[3] == 0.0F && transform.values[7] == 0.0F &&
           transform.values[11] == 0.0F && transform.values[15] == 1.0F;
}
} // namespace

std::expected<std::size_t, std::string> ResolveM2PrototypePropellerNode(
    const Assets::ModelAsset& model,
    const Assets::ModelVector3& assetBoundsCenter,
    const Physics::PhysicsVector3& expectedBodyLocalHubMeters,
    const float alignmentToleranceMeters)
{
    if (!std::isfinite(assetBoundsCenter.x) || !std::isfinite(assetBoundsCenter.y) ||
        !std::isfinite(assetBoundsCenter.z) || !expectedBodyLocalHubMeters.IsFinite() ||
        !std::isfinite(alignmentToleranceMeters) || alignmentToleranceMeters < 0.0F)
    {
        return std::unexpected("propeller node alignment inputs must be finite and tolerance non-negative");
    }

    std::size_t match = 0;
    std::size_t matchCount = 0;
    for (std::size_t index = 0; index < model.nodes.size(); ++index)
    {
        if (model.nodes[index].name == M2PrototypePropellerNodeName)
        {
            match = index;
            ++matchCount;
        }
    }
    if (matchCount != 1)
    {
        return std::unexpected(matchCount == 0 ? "canonical propeller node is missing"
                                               : "canonical propeller node is duplicated");
    }

    const Assets::ModelTransform& transform = model.nodes[match].localToModel;
    if (!IsFiniteAffine(transform))
    {
        return std::unexpected("canonical propeller node transform must be finite and affine");
    }
    const Physics::PhysicsVector3 authoredBodyLocalHub{
        transform.values[12] - assetBoundsCenter.x,
        transform.values[13] - assetBoundsCenter.y,
        transform.values[14] - assetBoundsCenter.z};
    if (std::abs(authoredBodyLocalHub.x - expectedBodyLocalHubMeters.x) > alignmentToleranceMeters ||
        std::abs(authoredBodyLocalHub.y - expectedBodyLocalHubMeters.y) > alignmentToleranceMeters ||
        std::abs(authoredBodyLocalHub.z - expectedBodyLocalHubMeters.z) > alignmentToleranceMeters)
    {
        return std::unexpected("canonical propeller hub does not align with Game propulsor tuning");
    }
    return match;
}

std::expected<float, std::string> AdvancePropellerPresentationAngle(
    const float currentAngleRadians,
    const float previousShaftRpm,
    const float nextShaftRpm,
    const float fixedDeltaSeconds)
{
    if (!std::isfinite(currentAngleRadians) || !std::isfinite(previousShaftRpm) ||
        !std::isfinite(nextShaftRpm) || !std::isfinite(fixedDeltaSeconds) || fixedDeltaSeconds <= 0.0F)
    {
        return std::unexpected("propeller presentation angle inputs must be finite and delta positive");
    }

    const double averageRpm =
        0.5 * (static_cast<double>(previousShaftRpm) + static_cast<double>(nextShaftRpm));
    const double deltaRadians = averageRpm * (TwoPiRadians / 60.0) * fixedDeltaSeconds;
    double wrapped = std::fmod(static_cast<double>(currentAngleRadians) + deltaRadians, TwoPiRadians);
    if (!std::isfinite(wrapped))
    {
        return std::unexpected("propeller presentation angle is not finite");
    }
    if (wrapped < 0.0)
    {
        wrapped += TwoPiRadians;
    }
    return static_cast<float>(wrapped);
}

std::expected<Assets::ModelTransform, std::string> RotationXTransform(const float angleRadians)
{
    if (!std::isfinite(angleRadians))
    {
        return std::unexpected("rotation angle must be finite");
    }
    const float cosine = std::cos(angleRadians);
    const float sine = std::sin(angleRadians);
    if (!std::isfinite(cosine) || !std::isfinite(sine))
    {
        return std::unexpected("rotation transform is not finite");
    }

    Assets::ModelTransform result;
    result.values[5] = cosine;
    result.values[6] = sine;
    result.values[9] = -sine;
    result.values[10] = cosine;
    return result;
}
}
