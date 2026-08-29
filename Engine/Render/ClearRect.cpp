#include "Engine/Render/ClearRect.h"

#include <algorithm>
#include <cmath>

namespace DeepRun::Render
{
bool RgbaColor::IsFinite() const noexcept
{
    return std::isfinite(r) && std::isfinite(g) && std::isfinite(b) && std::isfinite(a);
}

bool ViewportRect::IsFinite() const noexcept
{
    return std::isfinite(left) && std::isfinite(top) && std::isfinite(right) && std::isfinite(bottom);
}

std::expected<ViewportRect, std::string> ValidateViewportRect(const ViewportRect& rect)
{
    if (!rect.IsFinite())
    {
        return std::unexpected("viewport rect contains non-finite values");
    }
    if (rect.right <= rect.left || rect.bottom <= rect.top)
    {
        return std::unexpected("viewport rect is empty or inverted");
    }

    // Clamp into the normalized viewport. The Game projection helper clips against its own camera before
    // calling the renderer, so this only guards floating-point drift at the edges; a rect that ends up with
    // zero area after clamping was fully outside and must not reach the GPU as an invalid rectangle.
    const ViewportRect clamped{
        .left = std::clamp(rect.left, 0.0F, 1.0F),
        .top = std::clamp(rect.top, 0.0F, 1.0F),
        .right = std::clamp(rect.right, 0.0F, 1.0F),
        .bottom = std::clamp(rect.bottom, 0.0F, 1.0F)};
    if (clamped.right <= clamped.left || clamped.bottom <= clamped.top)
    {
        return std::unexpected("viewport rect is empty after clamping to the viewport");
    }
    return clamped;
}

std::expected<RgbaColor, std::string> ValidateRgbaColor(const RgbaColor& color)
{
    if (!color.IsFinite())
    {
        return std::unexpected("clear color contains non-finite values");
    }
    // The [0, 1] contract is enforced exactly: out-of-range components are rejected, never clamped.
    if (color.r < 0.0F || color.r > 1.0F || color.g < 0.0F || color.g > 1.0F || color.b < 0.0F ||
        color.b > 1.0F || color.a < 0.0F || color.a > 1.0F)
    {
        return std::unexpected("clear color component is outside [0, 1]");
    }
    return color;
}

std::expected<PixelRect, std::string> ToPixelRect(
    const ViewportRect& rect,
    const std::uint32_t width,
    const std::uint32_t height)
{
    if (width == 0 || height == 0)
    {
        return std::unexpected("render target has no size for pixel conversion");
    }
    if (!rect.IsFinite())
    {
        return std::unexpected("viewport rect contains non-finite values");
    }

    // Normalized viewport coordinates -> integer pixel rectangle. D3D12 rects use a top-left origin with
    // right/bottom exclusive, so left/top edges floor and right/bottom edges ceil (a tiny epsilon absorbs
    // floating-point drift at mathematically exact boundaries): a rect touching a viewport edge covers
    // exactly its pixels without gaps or overruns.
    constexpr float PixelEpsilon = 1.0e-6F;
    const auto floorPixel = [](const float value, const std::uint32_t extent) noexcept {
        return static_cast<std::uint32_t>(std::clamp(std::floor(value + PixelEpsilon), 0.0F,
                                                     static_cast<float>(extent)));
    };
    const auto ceilPixel = [](const float value, const std::uint32_t extent) noexcept {
        return static_cast<std::uint32_t>(std::clamp(std::ceil(value - PixelEpsilon), 1.0F,
                                                     static_cast<float>(extent)));
    };
    const PixelRect pixel{
        .left = floorPixel(rect.left * static_cast<float>(width), width),
        .top = floorPixel(rect.top * static_cast<float>(height), height),
        .right = ceilPixel(rect.right * static_cast<float>(width), width),
        .bottom = ceilPixel(rect.bottom * static_cast<float>(height), height)};
    if (pixel.right <= pixel.left || pixel.bottom <= pixel.top)
    {
        return std::unexpected("viewport rect collapses to an empty pixel rectangle");
    }
    return pixel;
}
}
