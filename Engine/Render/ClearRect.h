#pragma once

#include <cstdint>
#include <expected>
#include <string>

namespace DeepRun::Render
{
// Renderer-neutral RGBA color, each component in [0, 1]. Presentation-only data: the renderer has no
// knowledge of what a given color means to gameplay. The [0, 1] range is part of the contract and enforced
// by ValidateRgbaColor: non-finite or out-of-range components are recoverable errors, never silently
// clamped (M2 Slice D2 correction).
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

// A rectangle in integer pixel coordinates of a render target: top-left origin, right/bottom exclusive
// (D3D12 convention). Renderer-neutral — no D3D12 types leak through this API.
struct PixelRect final
{
    std::uint32_t left = 0;
    std::uint32_t top = 0;
    std::uint32_t right = 0;
    std::uint32_t bottom = 0;
};

// Pure validation for D3D12Renderer::ClearViewportRect color input, separated from D3D12 so it can be
// tested without a GPU (M2 Slice D2 correction). Contract: every component must be finite and within
// [0, 1]. Malformed colors are rejected as recoverable errors — the renderer never silently clamps a
// caller color. Valid colors pass through unchanged.
[[nodiscard]] std::expected<RgbaColor, std::string> ValidateRgbaColor(const RgbaColor& color);

// Pure normalized -> pixel conversion for D3D12Renderer::ClearViewportRect, separated from D3D12 so it can
// be tested without a GPU (M2 Slice D2 correction). Left/top edges floor and right/bottom edges ceil (a
// tiny epsilon absorbs floating-point drift at mathematically exact boundaries) so a rect touching a
// viewport edge covers exactly its pixels without gaps or overruns. Non-finite input, zero-size targets,
// and rects that collapse to zero pixel area are rejected: they must never reach the GPU as an invalid
// rectangle.
[[nodiscard]] std::expected<PixelRect, std::string> ToPixelRect(
    const ViewportRect& rect,
    std::uint32_t width,
    std::uint32_t height);
}
