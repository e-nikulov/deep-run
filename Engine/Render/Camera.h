#pragma once

#include "Engine/Assets/ModelAsset.h"

#include <array>
#include <expected>
#include <string>

namespace DeepRun::Render
{
struct RenderMatrix4 final
{
    // Column-major, matching ModelTransform and HLSL column-major matrices.
    std::array<float, 16> values{
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F};
};

struct OrthographicCamera final
{
    RenderMatrix4 view{};
    RenderMatrix4 projection{};
    RenderMatrix4 viewProjection{};
    Assets::ModelVector3 position{};
    Assets::ModelVector3 target{};
    Assets::ModelVector3 up{0.0F, 1.0F, 0.0F};
    Assets::ModelVector3 viewDirection{0.0F, 0.0F, -1.0F};
    float width = 0.0F;
    float height = 0.0F;
    float nearPlane = 0.0F;
    float farPlane = 0.0F;
};

[[nodiscard]] RenderMatrix4 Multiply(const RenderMatrix4& left, const RenderMatrix4& right) noexcept;
[[nodiscard]] std::array<float, 4> TransformPoint(
    const RenderMatrix4& matrix,
    const Assets::ModelVector3& point) noexcept;
[[nodiscard]] bool IsFinite(const RenderMatrix4& matrix) noexcept;
[[nodiscard]] std::expected<OrthographicCamera, std::string> BuildSideViewCamera(
    const Assets::ModelBounds& bounds,
    float aspectRatio,
    float margin = 1.12F);
[[nodiscard]] bool BoundsFitInCamera(
    const Assets::ModelBounds& bounds,
    const OrthographicCamera& camera,
    float epsilon = 1.0e-4F) noexcept;
}
