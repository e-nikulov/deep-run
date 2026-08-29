#pragma once

#include <expected>
#include <string>

namespace DeepRun::Render
{
// Renderer-neutral RGBA color, each component in [0, 1]. Presentation-only data: the renderer has no
// knowledge of what a given color means to gameplay.
struct RgbaColor final
{
    float r = 0.0F;
    float g = 0.0F;
    float b = 0.0F;
    float a = 1.0F;

    [[nodiscard]] bool IsFinite() const noexcept;
};

// A rectangle in normalized viewport coordinates: (0, 0) is the top-left corner of the current render
// target, (1, 1) its bottom-right corner. +X goes right, +Y goes down (screen convention). This type is
// deliberately generic — it carries no scene or gameplay semantics.
struct ViewportRect final
{
    float left = 0.0F;
    float top = 0.0F;
    float right = 1.0F;
    float bottom = 1.0F;

    [[nodiscard]] bool IsFinite() const noexcept;
};

// Pure validation/conversion for D3D12Renderer::ClearViewportRect input, separated from D3D12 so it can be
// tested without a GPU (M2 Slice D2). Policy: reject malformed input — non-finite values and empty/inverted
// rectangles are errors. Out-of-range coordinates are clamped into [0, 1]; the caller is expected to clip
// against its own projection before calling the renderer, so clamping here only guards against tiny
// floating-point drift at the viewport edges. A zero-area result after clamping (e.g. a rect fully outside
// the viewport) is rejected: it must never reach the GPU as an invalid rectangle.
[[nodiscard]] std::expected<ViewportRect, std::string> ValidateViewportRect(const ViewportRect& rect);
}
