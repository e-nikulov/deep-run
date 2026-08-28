#include "Engine/Render/Camera.h"

#include <algorithm>
#include <cmath>

namespace DeepRun::Render
{
namespace
{
float Element(const RenderMatrix4& matrix, const std::size_t row, const std::size_t column) noexcept
{
    return matrix.values[column * 4 + row];
}

void SetElement(
    RenderMatrix4& matrix,
    const std::size_t row,
    const std::size_t column,
    const float value) noexcept
{
    matrix.values[column * 4 + row] = value;
}

bool IsFinite(const Assets::ModelVector3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
}

RenderMatrix4 Multiply(const RenderMatrix4& left, const RenderMatrix4& right) noexcept
{
    RenderMatrix4 result;
    result.values.fill(0.0F);
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            float value = 0.0F;
            for (std::size_t inner = 0; inner < 4; ++inner)
            {
                value += Element(left, row, inner) * Element(right, inner, column);
            }
            SetElement(result, row, column, value);
        }
    }
    return result;
}

std::array<float, 4> TransformPoint(
    const RenderMatrix4& matrix,
    const Assets::ModelVector3& point) noexcept
{
    const std::array<float, 4> input{point.x, point.y, point.z, 1.0F};
    std::array<float, 4> result{};
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            result[row] += Element(matrix, row, column) * input[column];
        }
    }
    return result;
}

bool IsFinite(const RenderMatrix4& matrix) noexcept
{
    return std::ranges::all_of(matrix.values, [](const float value) { return std::isfinite(value); });
}

std::expected<OrthographicCamera, std::string> BuildSideViewCamera(
    const Assets::ModelBounds& bounds,
    const float aspectRatio,
    const float margin)
{
    if (!IsFinite(bounds.minimum) || !IsFinite(bounds.maximum) || !std::isfinite(aspectRatio) ||
        !std::isfinite(margin) || aspectRatio <= 0.0F || margin <= 1.0F ||
        bounds.maximum.x <= bounds.minimum.x || bounds.maximum.y <= bounds.minimum.y ||
        bounds.maximum.z < bounds.minimum.z)
    {
        return std::unexpected("side-view camera requires finite non-empty bounds and a positive aspect ratio");
    }

    OrthographicCamera camera;
    camera.target = {
        (bounds.minimum.x + bounds.maximum.x) * 0.5F,
        (bounds.minimum.y + bounds.maximum.y) * 0.5F,
        (bounds.minimum.z + bounds.maximum.z) * 0.5F};

    const float contentWidth = (bounds.maximum.x - bounds.minimum.x) * margin;
    const float contentHeight = (bounds.maximum.y - bounds.minimum.y) * margin;
    camera.height = std::max(contentHeight, contentWidth / aspectRatio);
    camera.width = camera.height * aspectRatio;

    const float depth = std::max(bounds.maximum.z - bounds.minimum.z, 1.0F);
    const float cameraDistance = std::max(contentWidth, contentHeight) * 0.75F + depth;
    camera.position = {camera.target.x, camera.target.y, bounds.maximum.z + cameraDistance};
    const float nearestGeometry = camera.position.z - bounds.maximum.z;
    const float farthestGeometry = camera.position.z - bounds.minimum.z;
    camera.nearPlane = std::max(0.1F, nearestGeometry * 0.5F);
    camera.farPlane = farthestGeometry + nearestGeometry * 0.5F;

    SetElement(camera.view, 0, 3, -camera.position.x);
    SetElement(camera.view, 1, 3, -camera.position.y);
    SetElement(camera.view, 2, 3, -camera.position.z);

    camera.projection.values.fill(0.0F);
    SetElement(camera.projection, 0, 0, 2.0F / camera.width);
    SetElement(camera.projection, 1, 1, 2.0F / camera.height);
    SetElement(camera.projection, 2, 2, 1.0F / (camera.nearPlane - camera.farPlane));
    SetElement(
        camera.projection,
        2,
        3,
        camera.nearPlane / (camera.nearPlane - camera.farPlane));
    SetElement(camera.projection, 3, 3, 1.0F);
    camera.viewProjection = Multiply(camera.projection, camera.view);

    if (!IsFinite(camera.view) || !IsFinite(camera.projection) || !IsFinite(camera.viewProjection))
    {
        return std::unexpected("side-view camera produced a non-finite matrix");
    }
    return camera;
}

bool BoundsFitInCamera(
    const Assets::ModelBounds& bounds,
    const OrthographicCamera& camera,
    const float epsilon) noexcept
{
    for (const float x : {bounds.minimum.x, bounds.maximum.x})
    {
        for (const float y : {bounds.minimum.y, bounds.maximum.y})
        {
            for (const float z : {bounds.minimum.z, bounds.maximum.z})
            {
                const std::array<float, 4> clip = TransformPoint(camera.viewProjection, {x, y, z});
                if (!std::isfinite(clip[0]) || !std::isfinite(clip[1]) || !std::isfinite(clip[2]) ||
                    std::abs(clip[3] - 1.0F) > epsilon || std::abs(clip[0]) > 1.0F + epsilon ||
                    std::abs(clip[1]) > 1.0F + epsilon || clip[2] < -epsilon || clip[2] > 1.0F + epsilon)
                {
                    return false;
                }
            }
        }
    }
    return true;
}
}
