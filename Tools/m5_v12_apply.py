from __future__ import annotations

from pathlib import Path


def read(path: str) -> str:
    return Path(path).read_text(encoding="utf-8")


def write(path: str, text: str) -> None:
    with Path(path).open("w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)


def replace_once(path: str, old: str, new: str) -> None:
    text = read(path)
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected one replacement, found {count}: {old[:90]!r}")
    write(path, text.replace(old, new, 1))


def replace_between(path: str, start: str, end: str, replacement: str) -> None:
    text = read(path)
    first = text.find(start)
    if first < 0 or text.find(start, first + len(start)) >= 0:
        raise RuntimeError(f"{path}: non-unique/missing start marker: {start!r}")
    last = text.find(end, first + len(start))
    if last < 0:
        raise RuntimeError(f"{path}: missing end marker: {end!r}")
    write(path, text[:first] + replacement + text[last:])


write("Game/Combat/CombatPlaygroundCamera.h", """#pragma once

#include \"Game/Combat/CombatPlaygroundRuntime.h\"

#include <cmath>
#include <expected>
#include <string>

namespace DeepRun::Game::Combat
{
// M5-V1.2 restores normal combat to a genuinely local underwater side view. The automatic director never
// zooms out to show the whole engagement: it keeps the accepted 800 m local scale and moves the presentation
// focus from ownship to the live torpedo/impact only after the weapon has cleared the local launch composition.
// The 3.6 km framing remains available only as an explicit tactical overview. This is presentation policy only;
// physics, perception, weapon guidance, target placement and damage authority are unchanged.
inline constexpr float M5CombatLocalCameraHorizontalSpanMeters = 800.0F;
inline constexpr float M5CombatTacticalCameraHorizontalSpanMeters = 3'600.0F;
inline constexpr float M5CombatCameraFollowTriggerProgressMeters = 250.0F;

enum class CombatPlaygroundCameraMode
{
    LocalLaunch,
    TorpedoFollow,
    ImpactFocus,
    TacticalOverview,
};

struct CombatPlaygroundCameraFraming final
{
    CombatPlaygroundCameraMode mode = CombatPlaygroundCameraMode::LocalLaunch;
    float targetOffsetXMeters = M5CombatCameraTargetOffsetXMeters;
    float horizontalSpanMeters = M5CombatLocalCameraHorizontalSpanMeters;
    float transitionProgress = 0.0F;
};

class CombatPlaygroundCameraDirector final
{
public:
    [[nodiscard]] std::expected<CombatPlaygroundCameraFraming, std::string> Evaluate(
        const CombatPlaygroundRuntime& runtime,
        const double simulationTimeSeconds)
    {
        if (!std::isfinite(simulationTimeSeconds) || simulationTimeSeconds < lastSimulationTimeSeconds_)
        {
            return std::unexpected(\"M5 combat camera received invalid or time-reversing SimulationTime\");
        }
        lastSimulationTimeSeconds_ = simulationTimeSeconds;

        const auto& torpedo = runtime.PlayerTorpedo();
        const auto& launchPosition = runtime.PlayerTorpedoLaunchPosition();
        if (!torpedo || !launchPosition)
        {
            mode_ = CombatPlaygroundCameraMode::LocalLaunch;
            return LocalFraming();
        }

        const float ownshipReferenceXMeters = launchPosition->x - M5CombatTorpedoLaunchClearanceMeters;
        const float torpedoOffsetXMeters = torpedo->positionMeters.x - ownshipReferenceXMeters;
        if (!std::isfinite(ownshipReferenceXMeters) || !std::isfinite(torpedoOffsetXMeters))
        {
            return std::unexpected(\"M5 combat camera received a non-finite contextual focus\");
        }

        if (torpedo->movementDomain == Weapons::MovementDomain::Underwater)
        {
            const float forwardProgressMeters = torpedo->positionMeters.x - launchPosition->x;
            if (!std::isfinite(forwardProgressMeters))
            {
                return std::unexpected(\"M5 combat camera received non-finite torpedo progress\");
            }
            if (forwardProgressMeters >= M5CombatCameraFollowTriggerProgressMeters)
            {
                mode_ = CombatPlaygroundCameraMode::TorpedoFollow;
                return ContextualFraming(CombatPlaygroundCameraMode::TorpedoFollow, torpedoOffsetXMeters);
            }
        }
        else if (torpedo->movementDomain == Weapons::MovementDomain::Spent)
        {
            mode_ = CombatPlaygroundCameraMode::ImpactFocus;
            return ContextualFraming(CombatPlaygroundCameraMode::ImpactFocus, torpedoOffsetXMeters);
        }

        mode_ = CombatPlaygroundCameraMode::LocalLaunch;
        return LocalFraming();
    }

    [[nodiscard]] CombatPlaygroundCameraMode Mode() const noexcept
    {
        return mode_;
    }

    [[nodiscard]] static constexpr CombatPlaygroundCameraFraming LocalFraming() noexcept
    {
        return CombatPlaygroundCameraFraming{
            .mode = CombatPlaygroundCameraMode::LocalLaunch,
            .targetOffsetXMeters = M5CombatCameraTargetOffsetXMeters,
            .horizontalSpanMeters = M5CombatLocalCameraHorizontalSpanMeters,
            .transitionProgress = 0.0F};
    }

    [[nodiscard]] static constexpr CombatPlaygroundCameraFraming TacticalFraming() noexcept
    {
        return CombatPlaygroundCameraFraming{
            .mode = CombatPlaygroundCameraMode::TacticalOverview,
            .targetOffsetXMeters = M5CombatCameraTargetOffsetXMeters,
            .horizontalSpanMeters = M5CombatTacticalCameraHorizontalSpanMeters,
            .transitionProgress = 1.0F};
    }

private:
    [[nodiscard]] static constexpr CombatPlaygroundCameraFraming ContextualFraming(
        const CombatPlaygroundCameraMode mode,
        const float targetOffsetXMeters) noexcept
    {
        return CombatPlaygroundCameraFraming{
            .mode = mode,
            .targetOffsetXMeters = targetOffsetXMeters,
            .horizontalSpanMeters = M5CombatLocalCameraHorizontalSpanMeters,
            .transitionProgress = 1.0F};
    }

    CombatPlaygroundCameraMode mode_ = CombatPlaygroundCameraMode::LocalLaunch;
    double lastSimulationTimeSeconds_ = 0.0;
};
} // namespace DeepRun::Game::Combat
""")

replace_once(
    "Game/Combat/CombatPlaygroundRuntime.h",
    """// M5 playground framing/weapon-profile tuning. The physical engagement is authored in kilometres; camera span
// is presentation policy and must not dictate target placement. The local launch frame places Antey at roughly
// one quarter of the view, while the later tactical overview reveals the remote target. These are gameplay
// values, not claimed real-world Project 949A or torpedo performance data.
inline constexpr float M5CombatCameraTargetOffsetXMeters = 400.0F;""",
    """// M5 playground framing/weapon-profile tuning. The physical engagement is authored in kilometres; camera span
// is presentation policy and must not dictate target placement. M5-V1.2 restores normal local framing around
// ownship; contextual follow/pan belongs to the production presentation camera and never changes actor state.
// These are gameplay values, not claimed real-world Project 949A or torpedo performance data.
inline constexpr float M5CombatCameraTargetOffsetXMeters = 0.0F;""")

replace_once(
    "Game/Camera/MultiScaleTacticalCamera.h",
    """// M5-H.3 presentation scale. World/simulation coordinates never change when the player zooms or pans.
// 80 m exposes close production-model inspection; 600 m remains the accepted M2/M3 local reference span;
// wider spans expose tactical, operational and strategic presentation without implying a physically loaded
// 600 km scene. M5-V1 starts normal combat at 500 m: with the authored ~100 m ownship depth this leaves the
// surface in the intended ~15% upper band while keeping the local seabed a secondary part of the frame.
inline constexpr float MultiScaleMinimumHorizontalSpanMeters = 80.0F;
inline constexpr float MultiScaleLocalReferenceHorizontalSpanMeters = 600.0F;
inline constexpr float MultiScaleMaximumHorizontalSpanMeters = 600'000.0F;
inline constexpr float MultiScaleInitialHorizontalSpanMeters = 500.0F;""",
    """// M5-H.3/M5-V1.2 presentation scale. World/simulation coordinates never change when the player zooms or pans.
// 80 m exposes close production-model inspection; 600 m remains the accepted M2/M3 reference; 800 m is the
// normal M5 underwater combat composition and the exact width of the canonical authored M3 local section.
// Wider spans are explicit tactical/operational/strategic presentation, not the default gameplay camera.
inline constexpr float MultiScaleMinimumHorizontalSpanMeters = 80.0F;
inline constexpr float MultiScaleLocalReferenceHorizontalSpanMeters = 600.0F;
inline constexpr float MultiScaleMaximumHorizontalSpanMeters = 600'000.0F;
inline constexpr float MultiScaleInitialHorizontalSpanMeters = 800.0F;""")

replace_once(
    "Game/Combat/CalmLaunchCameraAssist.h",
    """    // The M5 close-tactical scenario places the surface target around 1.8 km from ownship. A 4 km centered
    // frame contains both platforms without camera chase. The assist injects only a gentle zoom-out command;
    // it never pans or follows the torpedo and it runs at most once per launch.
    static constexpr float TargetHorizontalSpanMeters = 4'000.0F;""",
    """    // M5-V1.2 keeps launch assistance local: it may add a little breathing room for the weapon leaving
    // ownship, but it must never reveal the whole kilometre-scale engagement automatically. Target/pre-impact
    // framing is contextual presentation or explicit player navigation, not an all-battlefield zoom.
    static constexpr float TargetHorizontalSpanMeters = 1'000.0F;""")

replace_once(
    "Game/Environment/ScalableEnvironmentPresentation.h",
    """// M5-H.4/M5-V1 presentation policy. The accepted M3 environment remains the only authoritative local section.
// Reusing that 800 m section is acceptable only while the view is still genuinely local. Beyond 2 km the
// copied relief becomes more visually misleading than useful, so tactical-wide presentation uses one fixed,
// coarse strategic silhouette instead of wallpapering identical local geometry.
// No additional physics, navigation, acoustic terrain or gameplay state exists.
inline constexpr float M5DetailedEnvironmentMaximumHorizontalSpanMeters = 2'000.0F;
inline constexpr std::size_t M5DetailedEnvironmentMaximumVisibleTileCount = 16U;
inline constexpr float M5StrategicSeabedMinimumHorizontalSpanMeters =
    M5DetailedEnvironmentMaximumHorizontalSpanMeters;
inline constexpr float M5StrategicSeabedMaximumHorizontalSpanMeters = 12'000.0F;
inline constexpr float M5StrategicSeabedFillBottomYMeters = -650.0F;""",
    """// M5-V1.2 presentation policy. The accepted M3 800 m section remains the only detailed local section and is
// never wallpapered across the world. Once the camera no longer fits inside that authored section, a single
// coarse strategic silhouette is a temporary M5 render-only fallback. It is deliberately replaceable by a
// future shared deterministic/chunked bathymetry authority; no physics/navigation/acoustic state is created.
inline constexpr float M5DetailedEnvironmentMaximumHorizontalSpanMeters = 800.0F;
inline constexpr float M5StrategicSeabedMaximumHorizontalSpanMeters = 12'000.0F;
// Tactical fill is an open vertical skirt, not a closed slab. Its lower edge is deliberately far below every
// supported M5 combat frustum and there is no horizontal underside face to become visible.
inline constexpr float M5StrategicSeabedExtrusionBottomYMeters = -100'000.0F;""")

replace_once(
    "Game/Environment/ScalableEnvironmentPresentation.h",
    """[[nodiscard]] inline bool UseStrategicSeabedPresentation(const float cameraHorizontalSpanMeters) noexcept
{
    return std::isfinite(cameraHorizontalSpanMeters) &&
           cameraHorizontalSpanMeters > M5StrategicSeabedMinimumHorizontalSpanMeters &&
           cameraHorizontalSpanMeters <= M5StrategicSeabedMaximumHorizontalSpanMeters;
}""",
    """[[nodiscard]] inline bool UseStrategicSeabedPresentation(const float cameraHorizontalSpanMeters) noexcept
{
    return std::isfinite(cameraHorizontalSpanMeters) && cameraHorizontalSpanMeters > 0.0F &&
           cameraHorizontalSpanMeters <= M5StrategicSeabedMaximumHorizontalSpanMeters;
}""")

replace_once(
    "Game/Environment/ScalableEnvironmentPresentation.h",
    """    primitive.vertices.reserve((M5StrategicSeabedProfile.size() - 1U) * 16U);
    primitive.indices.reserve((M5StrategicSeabedProfile.size() - 1U) * 24U);""",
    """    primitive.vertices.reserve((M5StrategicSeabedProfile.size() - 1U) * 12U);
    primitive.indices.reserve((M5StrategicSeabedProfile.size() - 1U) * 18U);""")

replace_once(
    "Game/Environment/ScalableEnvironmentPresentation.h",
    """        pushVertex(first.xMeters, first.yMeters, M5StrategicSeabedBackZMeters, topNormal);
        pushVertex(second.xMeters, second.yMeters, M5StrategicSeabedBackZMeters, topNormal);
        pushVertex(second.xMeters, second.yMeters, M5StrategicSeabedFrontZMeters, topNormal);
        pushVertex(first.xMeters, first.yMeters, M5StrategicSeabedFrontZMeters, topNormal);
        pushVertex(first.xMeters, first.yMeters, M5StrategicSeabedFrontZMeters, {0.0F, 0.0F, 1.0F});
        pushVertex(second.xMeters, second.yMeters, M5StrategicSeabedFrontZMeters, {0.0F, 0.0F, 1.0F});
        pushVertex(second.xMeters, M5StrategicSeabedFillBottomYMeters, M5StrategicSeabedFrontZMeters,
                   {0.0F, 0.0F, 1.0F});
        pushVertex(first.xMeters, M5StrategicSeabedFillBottomYMeters, M5StrategicSeabedFrontZMeters,
                   {0.0F, 0.0F, 1.0F});
        pushVertex(first.xMeters, first.yMeters, M5StrategicSeabedBackZMeters, {0.0F, 0.0F, -1.0F});
        pushVertex(second.xMeters, second.yMeters, M5StrategicSeabedBackZMeters, {0.0F, 0.0F, -1.0F});
        pushVertex(second.xMeters, M5StrategicSeabedFillBottomYMeters, M5StrategicSeabedBackZMeters,
                   {0.0F, 0.0F, -1.0F});
        pushVertex(first.xMeters, M5StrategicSeabedFillBottomYMeters, M5StrategicSeabedBackZMeters,
                   {0.0F, 0.0F, -1.0F});
        pushVertex(first.xMeters, M5StrategicSeabedFillBottomYMeters, M5StrategicSeabedBackZMeters,
                   {0.0F, -1.0F, 0.0F});
        pushVertex(second.xMeters, M5StrategicSeabedFillBottomYMeters, M5StrategicSeabedBackZMeters,
                   {0.0F, -1.0F, 0.0F});
        pushVertex(second.xMeters, M5StrategicSeabedFillBottomYMeters, M5StrategicSeabedFrontZMeters,
                   {0.0F, -1.0F, 0.0F});
        pushVertex(first.xMeters, M5StrategicSeabedFillBottomYMeters, M5StrategicSeabedFrontZMeters,
                   {0.0F, -1.0F, 0.0F});

        pushTriangle(base, 0U, 2U, 1U);
        pushTriangle(base, 0U, 3U, 2U);
        pushTriangle(base, 4U, 6U, 5U);
        pushTriangle(base, 4U, 7U, 6U);
        pushTriangle(base, 8U, 9U, 10U);
        pushTriangle(base, 8U, 10U, 11U);
        pushTriangle(base, 12U, 13U, 14U);
        pushTriangle(base, 12U, 14U, 15U);""",
    """        pushVertex(first.xMeters, first.yMeters, M5StrategicSeabedBackZMeters, topNormal);
        pushVertex(second.xMeters, second.yMeters, M5StrategicSeabedBackZMeters, topNormal);
        pushVertex(second.xMeters, second.yMeters, M5StrategicSeabedFrontZMeters, topNormal);
        pushVertex(first.xMeters, first.yMeters, M5StrategicSeabedFrontZMeters, topNormal);
        pushVertex(first.xMeters, first.yMeters, M5StrategicSeabedFrontZMeters, {0.0F, 0.0F, 1.0F});
        pushVertex(second.xMeters, second.yMeters, M5StrategicSeabedFrontZMeters, {0.0F, 0.0F, 1.0F});
        pushVertex(second.xMeters, M5StrategicSeabedExtrusionBottomYMeters, M5StrategicSeabedFrontZMeters,
                   {0.0F, 0.0F, 1.0F});
        pushVertex(first.xMeters, M5StrategicSeabedExtrusionBottomYMeters, M5StrategicSeabedFrontZMeters,
                   {0.0F, 0.0F, 1.0F});
        pushVertex(first.xMeters, first.yMeters, M5StrategicSeabedBackZMeters, {0.0F, 0.0F, -1.0F});
        pushVertex(second.xMeters, second.yMeters, M5StrategicSeabedBackZMeters, {0.0F, 0.0F, -1.0F});
        pushVertex(second.xMeters, M5StrategicSeabedExtrusionBottomYMeters, M5StrategicSeabedBackZMeters,
                   {0.0F, 0.0F, -1.0F});
        pushVertex(first.xMeters, M5StrategicSeabedExtrusionBottomYMeters, M5StrategicSeabedBackZMeters,
                   {0.0F, 0.0F, -1.0F});

        pushTriangle(base, 0U, 2U, 1U);
        pushTriangle(base, 0U, 3U, 2U);
        pushTriangle(base, 4U, 6U, 5U);
        pushTriangle(base, 4U, 7U, 6U);
        pushTriangle(base, 8U, 9U, 10U);
        pushTriangle(base, 8U, 10U, 11U);""")

replace_between(
    "Game/Environment/ScalableEnvironmentPresentation.h",
    """    if (!UseDetailedEnvironmentPresentation(cameraHorizontalSpanMeters))
    {
        return std::vector<EnvironmentPresentationTile>{};
    }

    const double tileWidth""",
    """    return tiles;
}""",
    """    if (!UseDetailedEnvironmentPresentation(cameraHorizontalSpanMeters) ||
        !HorizontalPresentationBoundsCoverView(
            authoredMinimumX, authoredMaximumX, cameraTargetX, cameraHorizontalSpanMeters))
    {
        return std::vector<EnvironmentPresentationTile>{};
    }

    // M5-V1.2 never repeats the canonical local section. The temporary strategic presentation takes over
    // once this one authored section cannot cover the view. Keep the tile-shaped return type only to avoid
    // widening this scoped repair into a renderer API migration.
    static_cast<void>(verticalStepPerTileMeters);
    std::vector<EnvironmentPresentationTile> tiles;
    tiles.push_back(EnvironmentPresentationTile{.index = 0, .offsetXMeters = 0.0F, .offsetYMeters = 0.0F});
    return tiles;
}""")

replace_once(
    "Game/PhysicalPlayground.h",
    """    [[nodiscard]] std::expected<void, std::string> SetPresentationCameraFraming(
        const float targetOffsetXMeters,
        const float targetOffsetYMeters,
        const float horizontalSpanMeters)
    {
        if (!std::isfinite(targetOffsetXMeters) || !std::isfinite(targetOffsetYMeters) ||
            !std::isfinite(horizontalSpanMeters) || horizontalSpanMeters <= 0.0F)
        {
            return std::unexpected(\"physical playground presentation camera framing is invalid\");
        }

        // M5-V1 combat is underwater-first: when normal gameplay does not explicitly pan vertically, keep the
        // WaterBody surface near the upper 15% of a normal 16:9 view instead of centring the camera on the hull.
        // Explicit vertical camera input remains authoritative and the untouched 600 m M2/M3 path never calls
        // this setter, so its accepted fixed composition is unchanged.
        float effectiveTargetOffsetYMeters = targetOffsetYMeters;
        constexpr float M5NormalCombatReferenceAspectRatio = 16.0F / 9.0F;
        constexpr float M5NormalCombatAboveWaterFraction = 0.15F;
        constexpr float M5NormalCombatSurfaceBiasMinimumHorizontalSpanMeters = 600.0F;
        if (water_.has_value() && std::abs(targetOffsetYMeters) <= 1.0e-4F &&
            horizontalSpanMeters >= M5NormalCombatSurfaceBiasMinimumHorizontalSpanMeters)
        {
            const float unshiftedTargetYMeters =
                initialBodyWorldCenter_.y - presentationCameraTargetOffsetYMeters_;
            const float referenceVerticalSpanMeters =
                horizontalSpanMeters / M5NormalCombatReferenceAspectRatio;
            const float desiredTargetWorldYMeters =
                water_->Config().surfaceLevelY +
                (M5NormalCombatAboveWaterFraction - 0.5F) * referenceVerticalSpanMeters;
            effectiveTargetOffsetYMeters = desiredTargetWorldYMeters - unshiftedTargetYMeters;
        }

        initialBodyWorldCenter_.x += targetOffsetXMeters - presentationCameraTargetOffsetXMeters_;
        initialBodyWorldCenter_.y += effectiveTargetOffsetYMeters - presentationCameraTargetOffsetYMeters_;
        presentationCameraTargetOffsetXMeters_ = targetOffsetXMeters;
        presentationCameraTargetOffsetYMeters_ = effectiveTargetOffsetYMeters;
        presentationCameraHorizontalSpanMeters_ = horizontalSpanMeters;
        freePresentationCameraFraming_ = true;
        return {};
    }""",
    """    [[nodiscard]] std::expected<void, std::string> SetPresentationCameraFraming(
        const float targetOffsetXMeters,
        const float targetOffsetYMeters,
        const float horizontalSpanMeters,
        const float cameraAspectRatio)
    {
        if (!std::isfinite(targetOffsetXMeters) || !std::isfinite(targetOffsetYMeters) ||
            !std::isfinite(horizontalSpanMeters) || horizontalSpanMeters <= 0.0F ||
            !std::isfinite(cameraAspectRatio) || cameraAspectRatio <= 0.0F)
        {
            return std::unexpected(\"physical playground presentation camera framing is invalid\");
        }

        // M5-V1.2 underwater-first composition derives the vertical offset from the actual render-target aspect
        // every frame. This keeps the authoritative WaterBody surface at ~15% from the top through resize while
        // explicit vertical input remains authoritative. The untouched M2/M3 fixed path never calls this setter.
        float effectiveTargetOffsetYMeters = targetOffsetYMeters;
        constexpr float M5NormalCombatAboveWaterFraction = 0.15F;
        constexpr float M5NormalCombatSurfaceBiasMinimumHorizontalSpanMeters = 600.0F;
        if (water_.has_value() && std::abs(targetOffsetYMeters) <= 1.0e-4F &&
            horizontalSpanMeters >= M5NormalCombatSurfaceBiasMinimumHorizontalSpanMeters)
        {
            const float unshiftedTargetYMeters =
                initialBodyWorldCenter_.y - presentationCameraTargetOffsetYMeters_;
            const float verticalSpanMeters = horizontalSpanMeters / cameraAspectRatio;
            const float desiredTargetWorldYMeters =
                water_->Config().surfaceLevelY +
                (M5NormalCombatAboveWaterFraction - 0.5F) * verticalSpanMeters;
            effectiveTargetOffsetYMeters = desiredTargetWorldYMeters - unshiftedTargetYMeters;
        }

        initialBodyWorldCenter_.x += targetOffsetXMeters - presentationCameraTargetOffsetXMeters_;
        initialBodyWorldCenter_.y += effectiveTargetOffsetYMeters - presentationCameraTargetOffsetYMeters_;
        presentationCameraTargetOffsetXMeters_ = targetOffsetXMeters;
        presentationCameraTargetOffsetYMeters_ = effectiveTargetOffsetYMeters;
        presentationCameraHorizontalSpanMeters_ = horizontalSpanMeters;
        freePresentationCameraFraming_ = true;
        return {};
    }

    // Compatibility overload for older presentation-only call sites. Production M5 always supplies the live
    // renderer aspect through the four-argument overload so resize composition remains exact.
    [[nodiscard]] std::expected<void, std::string> SetPresentationCameraFraming(
        const float targetOffsetXMeters,
        const float targetOffsetYMeters,
        const float horizontalSpanMeters)
    {
        return SetPresentationCameraFraming(
            targetOffsetXMeters, targetOffsetYMeters, horizontalSpanMeters, 16.0F / 9.0F);
    }""")

replace_between(
    "Game/PhysicalPlayground.cpp",
    "// H.4 presentation-only continuation behind the repeated M3 seabed front wall.",
    "// M3-D fixed presentation tuning for the one canonical suspended-particulate field.",
    "// M3-D fixed presentation tuning for the one canonical suspended-particulate field.")

replace_between(
    "Game/PhysicalPlayground.cpp",
    "    // M5-H.4 scalable presentation: the accepted M3 section remains the sole local environment/physics",
    "    // The M3 Gerstner surface and suspended-particle field are authored as bounded local-detail envelopes.",
    """    // M5-V1.2 scalable presentation: the accepted M3 section remains the sole detailed local environment
    // section and is never tiled. Once span/pan no longer fits that section, the temporary strategic silhouette
    // supplies render-only bathymetry. Flora/fauna remain their single authored local instances and naturally
    // leave the view as the camera moves away; generic M5 combat never inherits the Arctic ice field.
    std::span<const Render::ModelDrawInstance> seabedPresentationDraws{seabedDraws_};
    std::span<const Render::ModelDrawInstance> strategicSeabedPresentationDraws{};
    std::span<const Render::ModelDrawInstance> floraPresentationDraws{floraDraws_};
    std::span<const Render::ModelDrawInstance> icePresentationDraws{iceDraws_};
    Render::ModelDrawInstance faunaDraw = faunaBaseDraw_;
    faunaDraw.modelToWorld = *faunaModelToWorld;
    std::span<const Render::ModelDrawInstance> faunaPresentationDraws(&faunaDraw, 1U);

    if (freePresentationCameraFraming_)
    {
        icePresentationDraws = {};

        constexpr float LocalSeabedBottomSafetyMarginMeters = 1.0F;
        const float cameraBottomWorldYMeters = camera->target.y - 0.5F * camera->height;
        const bool localSeabedCoversView =
            UseDetailedEnvironmentPresentation(camera->width) &&
            HorizontalPresentationBoundsCoverView(
                seabedSection_->renderGeometry.bounds.minimum.x,
                seabedSection_->renderGeometry.bounds.maximum.x,
                camera->target.x,
                camera->width) &&
            seabedSection_->renderGeometry.bounds.minimum.y <
                cameraBottomWorldYMeters - LocalSeabedBottomSafetyMarginMeters;

        if (!localSeabedCoversView)
        {
            seabedPresentationDraws = {};
            if (UseStrategicSeabedPresentation(camera->width))
            {
                strategicSeabedPresentationDraws = strategicSeabedDraws_;
            }
        }
    }

    // The M3 Gerstner surface and suspended-particle field are authored as bounded local-detail envelopes.""")

main = read("DeepRun/Main.cpp")
main = main.replace(
    """cameraFraming.targetOffsetXMeters,
                    0.0F,
                    cameraFraming.horizontalSpanMeters);""",
    """cameraFraming.targetOffsetXMeters,
                    0.0F,
                    cameraFraming.horizontalSpanMeters,
                    renderer->AspectRatio());""", 1)
main = main.replace(
    """multiScaleCamera.Framing().targetOffsetXMeters,
                    multiScaleCamera.Framing().targetOffsetYMeters,
                    multiScaleCamera.Framing().horizontalSpanMeters);""",
    """multiScaleCamera.Framing().targetOffsetXMeters,
                    multiScaleCamera.Framing().targetOffsetYMeters,
                    multiScaleCamera.Framing().horizontalSpanMeters,
                    renderer->AspectRatio());""", 1)
main = main.replace(
    """cameraFraming->targetOffsetXMeters,
                                0.0F,
                                cameraFraming->horizontalSpanMeters);""",
    """cameraFraming->targetOffsetXMeters,
                                0.0F,
                                cameraFraming->horizontalSpanMeters,
                                renderer.AspectRatio());""", 1)
main = main.replace(
    """cameraFraming->targetOffsetXMeters,
                            cameraFraming->targetOffsetYMeters,
                            cameraFraming->horizontalSpanMeters);""",
    """cameraFraming->targetOffsetXMeters,
                            cameraFraming->targetOffsetYMeters,
                            cameraFraming->horizontalSpanMeters,
                            renderer.AspectRatio());""", 1)
if main.count("renderer->AspectRatio());") < 2 or main.count("renderer.AspectRatio());") < 2:
    raise RuntimeError("DeepRun/Main.cpp: production aspect wiring failed")
# Existing early visual capture becomes the explicit normal-local M5 evidence in combat smoke.
main = main.replace(
    """const auto path = std::filesystem::path(\"m3_h1_frame_004.bmp\");
                        capturedInitial = WriteBmp(path, pixels, width, height);
                        std::cout << \"[Game] Captured initial visual frame to \" << path.string() << '\\n';""",
    """const auto path = std::filesystem::path(
                            combatPlayground.has_value() ? \"m5-normal-local.bmp\" : \"m3_h1_frame_004.bmp\");
                        capturedInitial = WriteBmp(path, pixels, width, height);
                        std::cout << \"[Game] Captured initial visual frame to \" << path.string() << '\\n';""", 1)
if main.count("m5-normal-local.bmp") != 1:
    raise RuntimeError("DeepRun/Main.cpp: normal-local capture wiring failed")
write("DeepRun/Main.cpp", main)

replace_between(
    "Game/Combat/CombatPlaygroundAcceptance.h",
    "        if (!Render::IsFinite(camera.view) || !Render::IsFinite(camera.projection) ||",
    "        // Capture must be delayed until this exact rendered state has passed through Present.",
    """        const float aboveWaterFraction =
            (camera.target.y + 0.5F * camera.height - surfaceLevelY_) / camera.height;
        if (!Render::IsFinite(camera.view) || !Render::IsFinite(camera.projection) ||
            !Render::IsFinite(camera.viewProjection) || !std::isfinite(camera.width) ||
            !std::isfinite(camera.height) || !std::isfinite(camera.nearPlane) || !std::isfinite(camera.farPlane) ||
            !std::isfinite(rendererAspectRatio) || rendererAspectRatio <= 0.0F ||
            camera.width < M5CombatLocalCameraHorizontalSpanMeters - 0.001F ||
            camera.width > M5CombatTacticalCameraHorizontalSpanMeters + 0.001F ||
            camera.height <= 0.0F || camera.nearPlane <= 0.0F || camera.farPlane <= camera.nearPlane ||
            !std::isfinite(aboveWaterFraction) || aboveWaterFraction < 0.099F || aboveWaterFraction > 0.201F ||
            !gpuPresentationHandleValid ||
            combatDrawStats.drawCalls < 2U || combatDrawStats.drawCalls > 5U ||
            combatDrawStats.submittedPrimitives != combatDrawStats.drawCalls ||
            combatDrawStats.submittedIndices != static_cast<std::uint64_t>(combatDrawStats.drawCalls) * 36U)
        {
            return std::unexpected(\"M5 visual acceptance render/camera/GPU contract failed\");
        }

        // Automated acceptance uses contextual normal-local framing for every mandatory capture. The explicit
        // 3.6 km tactical view is a separate player mode and must not be mistaken for normal gameplay framing.
        if (std::abs(camera.width - M5CombatLocalCameraHorizontalSpanMeters) > 0.01F)
        {
            return std::unexpected(\"M5 visual acceptance camera checkpoint left normal local scale\");
        }
        const bool ownshipCentredCheckpoint =
            *pending_ == M5CombatAcceptanceCheckpoint::Initial ||
            *pending_ == M5CombatAcceptanceCheckpoint::TorpedoInFlight;
        if (ownshipCentredCheckpoint)
        {
            if (std::abs(camera.target.x - latestFixedSnapshot_.anteyPositionMeters.x -
                         M5CombatCameraTargetOffsetXMeters) > 1.0F)
            {
                return std::unexpected(\"M5 normal local camera is not centred on Antey\");
            }
        }
        else if (presentationSnapshot.playerTorpedo.has_value() &&
                 std::abs(camera.target.x - presentationSnapshot.playerTorpedo->positionMeters.x) > 2.0F)
        {
            return std::unexpected(\"M5 engagement camera is not centred on the live torpedo/impact\");
        }

        // Capture must be delayed until this exact rendered state has passed through Present.""")

replace_once(
    "Game/Combat/CombatPlaygroundAcceptance.h",
    """        if (!pendingPresentedFrameAvailable_)
        {
            M5CombatAcceptanceSnapshot renderedSnapshot = pendingSnapshot_.value_or(latestFixedSnapshot_);""",
    """        if (!pendingPresentedFrameAvailable_)
        {
            if (*pending_ == M5CombatAcceptanceCheckpoint::PostImpact)
            {
                const auto authoritativeSnapshot = pendingSnapshot_.value_or(latestFixedSnapshot_);
                if (!presentationSnapshot.explosion.has_value() || !explosionDrawn)
                {
                    return std::optional<M5CombatAcceptanceRecord>{};
                }
                if (!IsM5PostImpactPresentationReady(authoritativeSnapshot, presentationSnapshot, explosionDrawn))
                {
                    return std::unexpected(\"M5 POST_IMPACT presentation explosion is not the authoritative impact\");
                }
            }

            M5CombatAcceptanceSnapshot renderedSnapshot = pendingSnapshot_.value_or(latestFixedSnapshot_);""")

replace_between(
    "Game/Combat/CombatPlaygroundAcceptance.h",
    "        // POST_IMPACT may only arm its capture after the real production render path has projected the live",
    "        M5CombatAcceptanceRecord record{",
    "        M5CombatAcceptanceRecord record{")

write("Tests/M5ScalableEnvironmentPresentationChecks.h", """#pragma once

#include \"Game/Combat/CombatPlaygroundCamera.h\"
#include \"Game/Environment/ScalableEnvironmentPresentation.h\"

#include <cmath>
#include <span>
#include <vector>

namespace DeepRun::Tests
{
[[nodiscard]] inline bool RunM5ScalableEnvironmentPresentationChecks()
{
    using namespace Game;

    if (!UseDetailedEnvironmentPresentation(600.0F) ||
        !UseDetailedEnvironmentPresentation(M5DetailedEnvironmentMaximumHorizontalSpanMeters) ||
        UseDetailedEnvironmentPresentation(M5DetailedEnvironmentMaximumHorizontalSpanMeters + 1.0F) ||
        UseDetailedEnvironmentPresentation(0.0F))
    {
        return false;
    }
    if (!UseStrategicSeabedPresentation(600.0F) ||
        !UseStrategicSeabedPresentation(Combat::M5CombatLocalCameraHorizontalSpanMeters) ||
        !UseStrategicSeabedPresentation(3'600.0F) ||
        !UseStrategicSeabedPresentation(M5StrategicSeabedMaximumHorizontalSpanMeters) ||
        UseStrategicSeabedPresentation(0.0F) ||
        UseStrategicSeabedPresentation(M5StrategicSeabedMaximumHorizontalSpanMeters + 1.0F))
    {
        return false;
    }

    const auto strategic = BuildStrategicSeabedPresentationModel();
    if (!strategic || strategic->primitives.size() != 1U ||
        strategic->primitives[0].vertices.size() != 192U ||
        strategic->primitives[0].indices.size() != 288U ||
        std::abs(strategic->bounds.minimum.y - M5StrategicSeabedExtrusionBottomYMeters) > 0.001F)
    {
        return false;
    }

    if (!HorizontalPresentationBoundsCoverView(-400.0F, 400.0F, 0.0F, 600.0F) ||
        !HorizontalPresentationBoundsCoverView(-400.0F, 400.0F, 0.0F, 800.0F) ||
        HorizontalPresentationBoundsCoverView(-400.0F, 400.0F, 120.0F, 600.0F) ||
        HorizontalPresentationBoundsCoverView(-400.0F, 400.0F, 0.0F, 801.0F))
    {
        return false;
    }

    const auto localTiles = BuildEnvironmentPresentationTiles(-400.0F, 400.0F, 0.0F, 600.0F, -5.0F);
    const auto exactLocalTiles = BuildEnvironmentPresentationTiles(-400.0F, 400.0F, 0.0F, 800.0F, -5.0F);
    const auto pannedTiles = BuildEnvironmentPresentationTiles(-400.0F, 400.0F, 120.0F, 600.0F, -5.0F);
    const auto widerTiles = BuildEnvironmentPresentationTiles(-400.0F, 400.0F, 0.0F, 801.0F, -5.0F);
    if (!localTiles || !exactLocalTiles || !pannedTiles || !widerTiles ||
        localTiles->size() != 1U || exactLocalTiles->size() != 1U ||
        !pannedTiles->empty() || !widerTiles->empty() ||
        localTiles->front().index != 0 || localTiles->front().offsetXMeters != 0.0F ||
        localTiles->front().offsetYMeters != 0.0F)
    {
        return false;
    }

    Render::ModelDrawInstance source{};
    source.modelToWorld.translation = {10.0F, 20.0F, 30.0F};
    source.materialBaseColorMultiplier = 0.5F;
    const std::vector<Render::ModelDrawInstance> localDraws = BuildEnvironmentPresentationDraws(
        std::span<const Render::ModelDrawInstance>(&source, 1U),
        std::span<const EnvironmentPresentationTile>(*localTiles));
    if (localDraws.size() != 1U ||
        std::abs(localDraws[0].modelToWorld.translation.x - 10.0F) > 0.001F ||
        std::abs(localDraws[0].modelToWorld.translation.y - 20.0F) > 0.001F ||
        std::abs(localDraws[0].materialBaseColorMultiplier - 0.5F) > 0.001F)
    {
        return false;
    }

    source.materialName = std::string(ScalableEnvironmentPresentationDetail::DefaultM5SuppressedTiledMaterial);
    const auto genericIce = BuildEnvironmentPresentationDraws(
        std::span<const Render::ModelDrawInstance>(&source, 1U),
        std::span<const EnvironmentPresentationTile>(*localTiles));
    const auto scenarioIce = BuildEnvironmentPresentationDraws(
        std::span<const Render::ModelDrawInstance>(&source, 1U),
        std::span<const EnvironmentPresentationTile>(*localTiles),
        ScalableEnvironmentPresentationDetail::DefaultM5SuppressedTiledMaterial);
    return genericIce.empty() && scenarioIce.size() == 1U;
}
} // namespace DeepRun::Tests
""")

replace_once(
    "Tests/M5CombatPlaygroundRuntimeChecks.h",
    """    bool sawStraightRunout = false;
    bool sawGradualAscent = false;
    bool sawCameraTransition = false;
    bool sawTacticalCamera = false;
    bool cameraEverLeftLocal = false;
    std::uint32_t stableTacticalTicks = 0U;
    float previousCameraSpan = Game::Combat::M5CombatLocalCameraHorizontalSpanMeters;""",
    """    bool sawStraightRunout = false;
    bool sawGradualAscent = false;
    bool sawLocalCamera = false;
    bool sawTorpedoFollowCamera = false;
    bool sawImpactFocusCamera = false;
    bool automaticCameraUsedTacticalOverview = false;""")

replace_between(
    "Tests/M5CombatPlaygroundRuntimeChecks.h",
    "        const auto cameraFraming = cameraDirector.Evaluate(runtime, simulationTimeSeconds);",
    "        for (const auto& track : frame->playerTracks)",
    """        const auto cameraFraming = cameraDirector.Evaluate(runtime, simulationTimeSeconds);
        if (!cameraFraming ||
            std::abs(cameraFraming->horizontalSpanMeters -
                     Game::Combat::M5CombatLocalCameraHorizontalSpanMeters) > 0.001F)
        {
            return false;
        }

        if (cameraFraming->mode == CombatPlaygroundCameraMode::LocalLaunch)
        {
            sawLocalCamera = true;
            if (std::abs(cameraFraming->targetOffsetXMeters -
                         Game::Combat::M5CombatCameraTargetOffsetXMeters) > 0.001F ||
                cameraFraming->transitionProgress != 0.0F)
            {
                return false;
            }
        }
        else if (cameraFraming->mode == CombatPlaygroundCameraMode::TorpedoFollow ||
                 cameraFraming->mode == CombatPlaygroundCameraMode::ImpactFocus)
        {
            if (!runtime.PlayerTorpedo() || !runtime.PlayerTorpedoLaunchPosition())
            {
                return false;
            }
            const float ownshipReferenceXMeters =
                runtime.PlayerTorpedoLaunchPosition()->x - Game::Combat::M5CombatTorpedoLaunchClearanceMeters;
            const float expectedOffset = runtime.PlayerTorpedo()->positionMeters.x - ownshipReferenceXMeters;
            if (std::abs(cameraFraming->targetOffsetXMeters - expectedOffset) > 0.01F ||
                std::abs(cameraFraming->transitionProgress - 1.0F) > 0.001F)
            {
                return false;
            }
            sawTorpedoFollowCamera = sawTorpedoFollowCamera ||
                cameraFraming->mode == CombatPlaygroundCameraMode::TorpedoFollow;
            sawImpactFocusCamera = sawImpactFocusCamera ||
                cameraFraming->mode == CombatPlaygroundCameraMode::ImpactFocus;
        }
        else if (cameraFraming->mode == CombatPlaygroundCameraMode::TacticalOverview)
        {
            automaticCameraUsedTacticalOverview = true;
        }

        for (const auto& track : frame->playerTracks)""")

replace_once(
    "Tests/M5CombatPlaygroundRuntimeChecks.h",
    """        !sawStraightRunout || !sawGradualAscent || !sawCameraTransition || !sawTacticalCamera ||
        stableTacticalTicks < 60U || !destroyerState ||""",
    """        !sawStraightRunout || !sawGradualAscent || !sawLocalCamera || !sawTorpedoFollowCamera ||
        !sawImpactFocusCamera || automaticCameraUsedTacticalOverview || !destroyerState ||""")

replace_once(
    "Tests/M5CombatPlaygroundRuntimeChecks.h",
    """    // The camera transition is one-way and SimulationTime authoritative. A time-reversing request must be
    // rejected instead of rewinding the cinematic framing back toward the submarine.
    if (cameraDirector.Evaluate(runtime, finalSimulationTimeSeconds - 1.0))
    {
        return false;
    }""",
    """    // Automatic combat framing stays local/contextual. The kilometre-scale composition remains available
    // only as an explicit tactical view, and SimulationTime reversal is still rejected.
    const auto tacticalFraming = CombatPlaygroundCameraDirector::TacticalFraming();
    if (tacticalFraming.mode != CombatPlaygroundCameraMode::TacticalOverview ||
        std::abs(tacticalFraming.horizontalSpanMeters -
                 Game::Combat::M5CombatTacticalCameraHorizontalSpanMeters) > 0.001F ||
        std::abs(tacticalFraming.targetOffsetXMeters -
                 Game::Combat::M5CombatCameraTargetOffsetXMeters) > 0.001F ||
        cameraDirector.Evaluate(runtime, finalSimulationTimeSeconds - 1.0))
    {
        return false;
    }""")

print("M5-V1.2 source transformation completed")
