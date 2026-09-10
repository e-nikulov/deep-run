#pragma once

#include "Engine/Input/InputState.h"

#include <algorithm>
#include <cmath>
#include <expected>
#include <string>

namespace DeepRun::Game::Camera
{
// M5-H.3 presentation scale. World/simulation coordinates never change when the player zooms or pans.
// 80 m exposes close production-model inspection; 600 m remains the accepted M2/M3 local reference span;
// wider spans expose tactical, operational and strategic presentation without implying a physically loaded
// 600 km scene.
inline constexpr float MultiScaleMinimumHorizontalSpanMeters = 80.0F;
inline constexpr float MultiScaleLocalReferenceHorizontalSpanMeters = 600.0F;
inline constexpr float MultiScaleMaximumHorizontalSpanMeters = 600'000.0F;
inline constexpr float MultiScaleInitialHorizontalSpanMeters = 1'600.0F;
inline constexpr float MultiScaleCloseInspectionMaximumOffsetMeters = 120.0F;

enum class MultiScaleCameraBand
{
    Detail,
    Local,
    Tactical,
    Operational,
    Strategic,
};

enum class MultiScalePresentationTier
{
    FullDetail,
    LocalSimplified,
    TacticalSimplified,
    OperationalSymbols,
    StrategicSymbols,
};

struct MultiScaleCameraFraming final
{
    float targetOffsetXMeters = 0.0F;
    // Reserved for a future vertical/depth-navigation slice. H.3 keeps it pinned to zero so the existing
    // M2/M3 vertical framing and water-surface composition remain authoritative.
    float targetOffsetYMeters = 0.0F;
    float horizontalSpanMeters = MultiScaleInitialHorizontalSpanMeters;
    float requestedHorizontalSpanMeters = MultiScaleInitialHorizontalSpanMeters;
    MultiScaleCameraBand band = MultiScaleCameraBand::Local;
    MultiScalePresentationTier presentationTier = MultiScalePresentationTier::FullDetail;
};

struct MultiScaleCameraInput final
{
    float panX = 0.0F;
    // Input keeps the semantic vertical axis reserved, but H.3 intentionally does not consume it.
    float panY = 0.0F;
    // Positive continuous zoom increases visible world span (zoom out); negative zooms in.
    float zoom = 0.0F;
    // Positive wheel steps conventionally zoom in, so they reduce the target span.
    float wheelSteps = 0.0F;
};

[[nodiscard]] inline MultiScaleCameraInput MultiScaleCameraInputFromState(
    const Input::InputState& input) noexcept
{
    return MultiScaleCameraInput{
        .panX = input.Axis(Input::InputAxis::CameraPanX),
        .panY = input.Axis(Input::InputAxis::CameraPanY),
        .zoom = input.Axis(Input::InputAxis::CameraZoom),
        .wheelSteps = input.CameraZoomSteps()};
}

[[nodiscard]] inline MultiScalePresentationTier PresentationTierForBand(
    const MultiScaleCameraBand band) noexcept
{
    switch (band)
    {
    case MultiScaleCameraBand::Detail: return MultiScalePresentationTier::FullDetail;
    case MultiScaleCameraBand::Local: return MultiScalePresentationTier::FullDetail;
    case MultiScaleCameraBand::Tactical: return MultiScalePresentationTier::TacticalSimplified;
    case MultiScaleCameraBand::Operational: return MultiScalePresentationTier::OperationalSymbols;
    case MultiScaleCameraBand::Strategic: return MultiScalePresentationTier::StrategicSymbols;
    }
    return MultiScalePresentationTier::FullDetail;
}

class MultiScaleTacticalCamera final
{
public:
    [[nodiscard]] std::expected<MultiScaleCameraFraming, std::string> Update(
        const MultiScaleCameraInput& input,
        const double presentationDeltaSeconds)
    {
        if (!std::isfinite(input.panX) || !std::isfinite(input.panY) || !std::isfinite(input.zoom) ||
            !std::isfinite(input.wheelSteps) || !std::isfinite(presentationDeltaSeconds) ||
            presentationDeltaSeconds < 0.0 || presentationDeltaSeconds > 1.0)
        {
            return std::unexpected("multi-scale camera received invalid presentation input");
        }

        const float panX = std::clamp(input.panX, -1.0F, 1.0F);
        const float zoom = std::clamp(input.zoom, -1.0F, 1.0F);
        const float deltaSeconds = static_cast<float>(presentationDeltaSeconds);

        // Multiplicative/logarithmic zoom makes one wheel notch meaningful at both 100 m and 100 km. Analog
        // controller zoom is deliberately slower and continuous. Neither path depends on SimulationTime.
        constexpr float WheelOctavesPerStep = 0.25F;
        constexpr float AnalogOctavesPerSecond = 1.35F;
        const float zoomOctaves = zoom * AnalogOctavesPerSecond * deltaSeconds -
                                  input.wheelSteps * WheelOctavesPerStep;
        if (zoomOctaves != 0.0F)
        {
            requestedSpanMeters_ = std::clamp(
                requestedSpanMeters_ * static_cast<float>(std::exp2(static_cast<double>(zoomOctaves))),
                MultiScaleMinimumHorizontalSpanMeters,
                MultiScaleMaximumHorizontalSpanMeters);
        }

        if (deltaSeconds > 0.0F)
        {
            constexpr float ZoomSmoothingSeconds = 0.16F;
            const float blend = 1.0F - static_cast<float>(std::exp(
                -static_cast<double>(deltaSeconds / ZoomSmoothingSeconds)));
            spanMeters_ += (requestedSpanMeters_ - spanMeters_) * std::clamp(blend, 0.0F, 1.0F);
            if (std::abs(spanMeters_ - requestedSpanMeters_) <=
                (std::max)(0.001F, requestedSpanMeters_ * 1.0e-5F))
            {
                spanMeters_ = requestedSpanMeters_;
            }

            constexpr float PanScreensPerSecond = 0.55F;
            targetOffsetXMeters_ += panX * spanMeters_ * PanScreensPerSecond * deltaSeconds;
        }

        ClampHorizontalPan();
        UpdateBandWithHysteresis();
        return Framing();
    }

    [[nodiscard]] std::expected<void, std::string> SetRequestedHorizontalSpanMeters(const float spanMeters)
    {
        if (!std::isfinite(spanMeters) || spanMeters < MultiScaleMinimumHorizontalSpanMeters ||
            spanMeters > MultiScaleMaximumHorizontalSpanMeters)
        {
            return std::unexpected("multi-scale camera requested span is outside the supported 80 m..600 km range");
        }
        requestedSpanMeters_ = spanMeters;
        return {};
    }

    [[nodiscard]] std::expected<void, std::string> FocusAtOffsets(
        const float offsetXMeters,
        const float offsetYMeters)
    {
        if (!std::isfinite(offsetXMeters) || !std::isfinite(offsetYMeters))
        {
            return std::unexpected("multi-scale camera focus offsets must be finite");
        }
        if (std::abs(offsetYMeters) > 1.0e-4F)
        {
            return std::unexpected("M5-H.3 vertical camera focus is not supported");
        }
        targetOffsetXMeters_ = offsetXMeters;
        ClampHorizontalPan();
        return {};
    }

    void FocusOwnship() noexcept
    {
        targetOffsetXMeters_ = 0.0F;
    }

    [[nodiscard]] MultiScaleCameraFraming Framing() const noexcept
    {
        return MultiScaleCameraFraming{
            .targetOffsetXMeters = targetOffsetXMeters_,
            .targetOffsetYMeters = 0.0F,
            .horizontalSpanMeters = spanMeters_,
            .requestedHorizontalSpanMeters = requestedSpanMeters_,
            .band = band_,
            .presentationTier = PresentationTierForBand(band_)};
    }

private:
    void ClampHorizontalPan() noexcept
    {
        // Close inspection intentionally permits panning along the 154 m production hull even when the camera
        // is narrower than the vessel. At tactical/strategic spans the old ownship-anchored rule takes over
        // progressively; no camera operation moves simulation state.
        const float wideOffsetMeters = 0.5F *
            (std::max)(0.0F, spanMeters_ - MultiScaleLocalReferenceHorizontalSpanMeters);
        const float maximumOffsetMeters =
            (std::max)(MultiScaleCloseInspectionMaximumOffsetMeters, wideOffsetMeters);
        targetOffsetXMeters_ = std::clamp(
            targetOffsetXMeters_, -maximumOffsetMeters, maximumOffsetMeters);
    }

    void UpdateBandWithHysteresis() noexcept
    {
        // Close inspection is a presentation-only extension below the accepted 600 m local reference. The
        // engineering representation contract remains approximately 0-2 km full detail, 2-8 km simplified
        // and 8 km+ contact/symbol presentation. Enter/leave thresholds differ to prevent flicker.
        bool changed = true;
        while (changed)
        {
            changed = false;
            switch (band_)
            {
            case MultiScaleCameraBand::Detail:
                if (spanMeters_ > 320.0F) { band_ = MultiScaleCameraBand::Local; changed = true; }
                break;
            case MultiScaleCameraBand::Local:
                if (spanMeters_ < 240.0F) { band_ = MultiScaleCameraBand::Detail; changed = true; }
                else if (spanMeters_ > 2'300.0F) { band_ = MultiScaleCameraBand::Tactical; changed = true; }
                break;
            case MultiScaleCameraBand::Tactical:
                if (spanMeters_ < 1'700.0F) { band_ = MultiScaleCameraBand::Local; changed = true; }
                else if (spanMeters_ > 9'000.0F) { band_ = MultiScaleCameraBand::Operational; changed = true; }
                break;
            case MultiScaleCameraBand::Operational:
                if (spanMeters_ < 7'000.0F) { band_ = MultiScaleCameraBand::Tactical; changed = true; }
                else if (spanMeters_ > 120'000.0F) { band_ = MultiScaleCameraBand::Strategic; changed = true; }
                break;
            case MultiScaleCameraBand::Strategic:
                if (spanMeters_ < 80'000.0F) { band_ = MultiScaleCameraBand::Operational; changed = true; }
                break;
            }
        }
    }

    float targetOffsetXMeters_ = 0.0F;
    float spanMeters_ = MultiScaleInitialHorizontalSpanMeters;
    float requestedSpanMeters_ = MultiScaleInitialHorizontalSpanMeters;
    MultiScaleCameraBand band_ = MultiScaleCameraBand::Local;
};
} // namespace DeepRun::Game::Camera
