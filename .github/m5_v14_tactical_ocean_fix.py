from pathlib import Path
import re

ROOT = Path('.')


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f'{label}: expected exactly one match, found {count}')
    return text.replace(old, new, 1)


header_path = ROOT / 'Game/Environment/ScalableEnvironmentPresentation.h'
header = header_path.read_text(encoding='utf-8')
header = replace_once(
    header,
    "inline constexpr float M5StrategicSeabedMaximumHorizontalSpanMeters = 12'000.0F;",
    "inline constexpr float M5StrategicSeabedMaximumHorizontalSpanMeters = 120'000.0F;",
    'strategic maximum span')

profile_pattern = re.compile(
    r'// One finite, authored low-frequency silhouette for M5 tactical-wide presentation\..*?'
    r'inline constexpr std::array<StrategicSeabedPresentationPoint, 28> M5StrategicSeabedProfile\{\{.*?\}\};\n',
    re.S)
profile_replacement = r'''// M5 tactical bathymetry is presentation-only regional context. The central anchors reproduce the accepted
// local M3 section exactly; outside that section a deterministic low-frequency profile descends gradually
// through continental/deep-ocean water while adding several broad ridges and trenches. This prevents the
// 800 m section from becoming one giant tactical "mountain" without pretending to be final world authority.
inline constexpr std::array<StrategicSeabedPresentationPoint, 14> M5LocalBathymetryAnchorProfile{{
    {-400.0F, -160.0F}, {-320.0F, -155.0F}, {-250.0F, -150.0F},
    {-180.0F, -125.0F}, {-130.0F, -120.0F}, {-90.0F, -132.0F},
    {-45.0F, -140.0F}, {-20.0F, -205.0F}, {0.0F, -220.0F},
    {80.0F, -220.0F}, {120.0F, -205.0F}, {180.0F, -180.0F},
    {260.0F, -170.0F}, {400.0F, -165.0F}}};

[[nodiscard]] inline float SampleM5TacticalBathymetryYMeters(const float xMeters) noexcept
{
    if (!std::isfinite(xMeters))
    {
        return -220.0F;
    }

    if (xMeters >= M5LocalBathymetryAnchorProfile.front().xMeters &&
        xMeters <= M5LocalBathymetryAnchorProfile.back().xMeters)
    {
        for (std::size_t index = 0U; index + 1U < M5LocalBathymetryAnchorProfile.size(); ++index)
        {
            const auto first = M5LocalBathymetryAnchorProfile[index];
            const auto second = M5LocalBathymetryAnchorProfile[index + 1U];
            if (xMeters >= first.xMeters && xMeters <= second.xMeters)
            {
                const float span = second.xMeters - first.xMeters;
                const float t = span > 0.0F ? (xMeters - first.xMeters) / span : 0.0F;
                return first.yMeters + (second.yMeters - first.yMeters) * t;
            }
        }
    }

    const float distanceFromLocalMeters = (std::max)(0.0F, std::abs(xMeters) - 400.0F);
    const float baseDepthMeters = 165.0F + (std::min)(3'600.0F, distanceFromLocalMeters * 0.055F);
    const float reliefWeight = std::clamp(distanceFromLocalMeters / 2'000.0F, 0.0F, 1.0F);
    const float regionalReliefMeters = reliefWeight * (
        85.0F * std::sin(xMeters / 1'900.0F) +
        55.0F * std::sin(xMeters / 710.0F + 0.8F) +
        35.0F * std::sin(xMeters / 3'300.0F + 1.6F));
    const float depthMeters = std::clamp(baseDepthMeters + regionalReliefMeters, 150.0F, 4'800.0F);
    return -depthMeters;
}

[[nodiscard]] inline std::vector<StrategicSeabedPresentationPoint> BuildM5TacticalBathymetryProfile()
{
    constexpr int ExtentKilometers = 60;
    std::vector<StrategicSeabedPresentationPoint> result;
    result.reserve(static_cast<std::size_t>(ExtentKilometers * 2) + M5LocalBathymetryAnchorProfile.size());

    for (int kilometer = -ExtentKilometers; kilometer <= -1; ++kilometer)
    {
        const float xMeters = static_cast<float>(kilometer) * 1'000.0F;
        result.push_back({xMeters, SampleM5TacticalBathymetryYMeters(xMeters)});
    }
    result.insert(result.end(), M5LocalBathymetryAnchorProfile.begin(), M5LocalBathymetryAnchorProfile.end());
    for (int kilometer = 1; kilometer <= ExtentKilometers; ++kilometer)
    {
        const float xMeters = static_cast<float>(kilometer) * 1'000.0F;
        result.push_back({xMeters, SampleM5TacticalBathymetryYMeters(xMeters)});
    }
    return result;
}

struct DaySkyPresentationBand final
{
    Render::ViewportRect viewport{};
    Render::RgbaColor color{};
};

struct DaySunPresentationStrip final
{
    Render::ViewportRect viewport{};
    Render::RgbaColor color{};
};

[[nodiscard]] inline std::expected<std::vector<DaySkyPresentationBand>, std::string>
BuildM5DaySkyPresentationBands(const float waterlineViewportY)
{
    if (!std::isfinite(waterlineViewportY))
    {
        return std::unexpected("day-sky waterline is not finite");
    }

    const float skyBottom = std::clamp(waterlineViewportY, 0.0F, 1.0F);
    if (skyBottom <= 1.0e-4F)
    {
        return std::vector<DaySkyPresentationBand>{};
    }

    constexpr std::size_t BandCount = 16U;
    constexpr Render::RgbaColor ZenithColor{0.012F, 0.055F, 0.180F, 1.0F};
    constexpr Render::RgbaColor HorizonColor{0.180F, 0.350F, 0.620F, 1.0F};
    const auto lerp = [](const float first, const float second, const float t) noexcept
    {
        return first + (second - first) * t;
    };

    std::vector<DaySkyPresentationBand> result;
    result.reserve(BandCount);
    for (std::size_t index = 0U; index < BandCount; ++index)
    {
        const float top = skyBottom * static_cast<float>(index) / static_cast<float>(BandCount);
        const float bottom = skyBottom * static_cast<float>(index + 1U) / static_cast<float>(BandCount);
        const float linearT = static_cast<float>(index) / static_cast<float>(BandCount - 1U);
        const float smoothT = linearT * linearT * (3.0F - 2.0F * linearT);
        result.push_back(DaySkyPresentationBand{
            .viewport = {.left = 0.0F, .top = top, .right = 1.0F, .bottom = bottom},
            .color = {
                lerp(ZenithColor.r, HorizonColor.r, smoothT),
                lerp(ZenithColor.g, HorizonColor.g, smoothT),
                lerp(ZenithColor.b, HorizonColor.b, smoothT),
                1.0F}});
    }
    return result;
}

[[nodiscard]] inline std::expected<std::vector<DaySunPresentationStrip>, std::string>
BuildM5DaySunPresentationStrips(const float waterlineViewportY, const float aspectRatio)
{
    if (!std::isfinite(waterlineViewportY) || !std::isfinite(aspectRatio) || !(aspectRatio > 0.0F))
    {
        return std::unexpected("day-sun presentation received invalid framing");
    }

    const float skyBottom = std::clamp(waterlineViewportY, 0.0F, 1.0F);
    if (skyBottom < 0.04F)
    {
        return std::vector<DaySunPresentationStrip>{};
    }

    constexpr std::array<float, 9> RowWidths{{0.45F, 0.70F, 0.88F, 0.98F, 1.0F, 0.98F, 0.88F, 0.70F, 0.45F}};
    constexpr Render::RgbaColor SunColor{1.0F, 0.78F, 0.30F, 1.0F};
    const float diameterY = (std::min)(0.036F, skyBottom * 0.30F);
    const float diameterX = diameterY / aspectRatio;
    const float centerX = 0.70F;
    const float centerY = skyBottom * 0.38F;
    const float rowHeight = diameterY / static_cast<float>(RowWidths.size());

    std::vector<DaySunPresentationStrip> result;
    result.reserve(RowWidths.size());
    for (std::size_t row = 0U; row < RowWidths.size(); ++row)
    {
        const float width = diameterX * RowWidths[row];
        const float top = centerY - 0.5F * diameterY + static_cast<float>(row) * rowHeight;
        const float bottom = (std::min)(skyBottom, top + rowHeight);
        if (bottom <= top)
        {
            continue;
        }
        result.push_back(DaySunPresentationStrip{
            .viewport = {
                .left = std::clamp(centerX - 0.5F * width, 0.0F, 1.0F),
                .top = std::clamp(top, 0.0F, skyBottom),
                .right = std::clamp(centerX + 0.5F * width, 0.0F, 1.0F),
                .bottom = std::clamp(bottom, 0.0F, skyBottom)},
            .color = SunColor});
    }
    return result;
}
'''
header, count = profile_pattern.subn(profile_replacement, header, count=1)
if count != 1:
    raise RuntimeError(f'profile replacement: expected 1 match, found {count}')

header = replace_once(
    header,
    '    Assets::MeshPrimitiveData primitive;\n',
    '    const auto profile = BuildM5TacticalBathymetryProfile();\n'
    '    if (profile.size() < 2U)\n'
    '    {\n'
    '        return std::unexpected("strategic seabed presentation profile is empty");\n'
    '    }\n\n'
    '    Assets::MeshPrimitiveData primitive;\n',
    'profile local variable')
header = header.replace('M5StrategicSeabedProfile.size()', 'profile.size()')
header = header.replace('M5StrategicSeabedProfile[index]', 'profile[index]')
header = header.replace('M5StrategicSeabedProfile[index + 1U]', 'profile[index + 1U]')
if 'M5StrategicSeabedProfile' in header:
    raise RuntimeError('obsolete fixed strategic profile reference remains')
header_path.write_text(header, encoding='utf-8')


playground_path = ROOT / 'Game/PhysicalPlayground.cpp'
playground = playground_path.read_text(encoding='utf-8')
sky_pattern = re.compile(
    r'    // M3-E keeps the Game-owned full above-water clear\..*?'
    r'    const auto underwaterRegion = UnderwaterRegionForSurface\(\*camera, water_->Config\(\)\.surfaceLevelY\);',
    re.S)
sky_replacement = r'''    // Legacy M2/M3 benchmark presentation keeps its original dark above-water clear. M5 free-presentation
    // gameplay uses a deterministic DAY baseline instead: a scene-linear sky gradient plus a small sun cue.
    // This is presentation-only and intentionally not a time-of-day simulation; future weather/day-night work
    // may replace it without touching WaterBody or combat authority.
    const auto waterlineViewportY = ProjectWorldSurfaceToViewportY(*camera, water_->Config().surfaceLevelY);
    if (!waterlineViewportY)
    {
        return std::unexpected("physical playground waterline projection failed: " + waterlineViewportY.error());
    }

    if (freePresentationCameraFraming_)
    {
        const auto skyBands = BuildM5DaySkyPresentationBands(*waterlineViewportY);
        if (!skyBands)
        {
            return std::unexpected("physical playground day-sky presentation failed: " + skyBands.error());
        }
        for (const auto& band : *skyBands)
        {
            const auto cleared = renderer.ClearViewportRect(band.viewport, band.color);
            if (!cleared)
            {
                return std::unexpected("physical playground day-sky clear failed: " + cleared.error());
            }
        }

        const auto sunStrips = BuildM5DaySunPresentationStrips(*waterlineViewportY, renderer.AspectRatio());
        if (!sunStrips)
        {
            return std::unexpected("physical playground day-sun presentation failed: " + sunStrips.error());
        }
        for (const auto& strip : *sunStrips)
        {
            const auto cleared = renderer.ClearViewportRect(strip.viewport, strip.color);
            if (!cleared)
            {
                return std::unexpected("physical playground day-sun clear failed: " + cleared.error());
            }
        }
    }
    else
    {
        const auto aboveWaterCleared = renderer.ClearViewportRect(
            Render::ViewportRect{}, // full viewport: default {0, 0, 1, 1}
            M2AboveWaterBackgroundColor);
        if (!aboveWaterCleared)
        {
            return std::unexpected(aboveWaterCleared.error());
        }
    }

    const auto underwaterRegion = UnderwaterRegionForSurface(*camera, water_->Config().surfaceLevelY);'''
playground, count = sky_pattern.subn(sky_replacement, playground, count=1)
if count != 1:
    raise RuntimeError(f'sky presentation replacement: expected 1 match, found {count}')
playground_path.write_text(playground, encoding='utf-8')


tests_path = ROOT / 'Tests/M5ScalableEnvironmentPresentationChecks.h'
tests = tests_path.read_text(encoding='utf-8')
old_profile_test_pattern = re.compile(
    r'    // Tactical context preserves the accepted local profile at the centre and descends into deep water\n'
    r'    // away from it; the wide LOD therefore cannot become a flat replacement floor\.\n'
    r'    if \(M5StrategicSeabedProfile\.size\(\) != 28U \|\|.*?\n    \}\n\n'
    r'    const auto strategic = BuildStrategicSeabedPresentationModel\(\);\n'
    r'    if \(!strategic \|\| strategic->primitives\.size\(\) != 1U \|\|\n'
    r'        strategic->primitives\[0\]\.vertices\.size\(\) != 324U \|\|\n'
    r'        strategic->primitives\[0\]\.indices\.size\(\) != 486U \|\|\n'
    r'        std::abs\(strategic->bounds\.minimum\.y - M5StrategicSeabedExtrusionBottomYMeters\) > 0\.001F\)\n'
    r'    \{\n        return false;\n    \}\n',
    re.S)
new_profile_test = r'''    // The tactical profile keeps exact local anchors but no longer collapses into one giant central hill.
    // Regional samples descend gradually and contain low-frequency relief before reaching deep ocean.
    const auto tacticalProfile = BuildM5TacticalBathymetryProfile();
    const auto findPoint = [&tacticalProfile](const float xMeters) -> const StrategicSeabedPresentationPoint*
    {
        for (const auto& point : tacticalProfile)
        {
            if (std::abs(point.xMeters - xMeters) < 0.001F)
            {
                return &point;
            }
        }
        return nullptr;
    };
    const auto* leftLocal = findPoint(-400.0F);
    const auto* centerLocal = findPoint(0.0F);
    const auto* rightLocal = findPoint(400.0F);
    const auto* nearRight = findPoint(1'000.0F);
    const auto* regionalRight = findPoint(6'000.0F);
    const auto* deepRight = findPoint(20'000.0F);
    if (tacticalProfile.size() < 120U ||
        tacticalProfile.front().xMeters > -59'000.0F || tacticalProfile.back().xMeters < 59'000.0F ||
        leftLocal == nullptr || centerLocal == nullptr || rightLocal == nullptr || nearRight == nullptr ||
        regionalRight == nullptr || deepRight == nullptr ||
        leftLocal->yMeters != -160.0F || centerLocal->yMeters != -220.0F || rightLocal->yMeters != -165.0F ||
        nearRight->yMeters > -150.0F || nearRight->yMeters < -350.0F ||
        regionalRight->yMeters >= nearRight->yMeters || deepRight->yMeters >= -700.0F ||
        std::abs(SampleM5TacticalBathymetryYMeters(6'000.0F) -
                 SampleM5TacticalBathymetryYMeters(8'000.0F)) < 5.0F)
    {
        return false;
    }

    const auto strategic = BuildStrategicSeabedPresentationModel();
    const std::size_t expectedStrategicSegments = tacticalProfile.size() - 1U;
    if (!strategic || strategic->primitives.size() != 1U ||
        strategic->primitives[0].vertices.size() != expectedStrategicSegments * 12U ||
        strategic->primitives[0].indices.size() != expectedStrategicSegments * 18U ||
        std::abs(strategic->bounds.minimum.y - M5StrategicSeabedExtrusionBottomYMeters) > 0.001F)
    {
        return false;
    }
'''
tests, count = old_profile_test_pattern.subn(new_profile_test, tests, count=1)
if count != 1:
    raise RuntimeError(f'profile test replacement: expected 1 match, found {count}')

sky_test_anchor = '''    for (std::size_t index = 1U; index < abyssBands->size(); ++index)
    {
        const auto& previous = (*abyssBands)[index - 1U];
        const auto& current = (*abyssBands)[index];
        if (std::abs(previous.viewport.bottom - current.viewport.top) > 0.0001F ||
            current.color.r > previous.color.r || current.color.g > previous.color.g ||
            current.color.b > previous.color.b ||
            std::abs(current.color.b - previous.color.b) > 0.01F)
        {
            return false;
        }
    }

'''
sky_tests = sky_test_anchor + '''    const auto daySky = BuildM5DaySkyPresentationBands(0.32F);
    const auto daySun = BuildM5DaySunPresentationStrips(0.32F, 16.0F / 9.0F);
    const auto noSky = BuildM5DaySkyPresentationBands(0.0F);
    const auto invalidSun = BuildM5DaySunPresentationStrips(0.32F, 0.0F);
    if (!daySky || daySky->size() != 16U || !daySun || daySun->size() != 9U ||
        !noSky || !noSky->empty() || invalidSun.has_value() ||
        std::abs(daySky->front().viewport.top) > 0.0001F ||
        std::abs(daySky->back().viewport.bottom - 0.32F) > 0.0001F ||
        !(daySky->back().color.r > daySky->front().color.r) ||
        !(daySky->back().color.g > daySky->front().color.g) ||
        !(daySky->back().color.b > daySky->front().color.b))
    {
        return false;
    }
    for (const auto& strip : *daySun)
    {
        if (!(strip.viewport.left < strip.viewport.right) || !(strip.viewport.top < strip.viewport.bottom) ||
            strip.viewport.top < 0.0F || strip.viewport.bottom > 0.32F)
        {
            return false;
        }
    }

'''
tests = replace_once(tests, sky_test_anchor, sky_tests, 'day sky tests')
tests_path.write_text(tests, encoding='utf-8')


doc_path = ROOT / 'docs/design/vertical-ocean-gameplay.md'
doc = doc_path.read_text(encoding='utf-8')
doc = replace_once(
    doc,
    '- any temporary strategic seabed mesh is explicit scenario opt-in only.\n',
    '- the current M5 combat region may opt in to a render-only tactical bathymetry continuity profile whose local anchors match the accepted M3 section; unknown/deep regions still use the abyss presentation.\n'
    '- M5 free-presentation uses a deterministic DAY sky baseline (scene-linear sky gradient plus sun cue); full time-of-day, weather and moon/night presentation remain future content scope.\n',
    'M5 applicability update')

day_section = '''\n## M5 tactical sky baseline\n\nM5 manual acceptance uses a deterministic daytime presentation so increased tactical sky share reads as intentional airspace rather than an unfinished black clear. The production free-presentation path therefore shows a blue vertical sky gradient and a small sun cue above the authoritative sea surface. This is not a time-of-day simulation. Future weather/day-night work may replace the M5 baseline with scenario-owned sun/moon/lighting state while preserving the same surface/airspace composition contract.\n\n'''
doc = replace_once(doc, '\n## Authority boundary\n', day_section + '## Authority boundary\n', 'day sky documentation section')
doc_path.write_text(doc, encoding='utf-8')

print('M5 V1.4 tactical ocean / day-sky repair applied')
