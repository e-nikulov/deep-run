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
}
