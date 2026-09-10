$ErrorActionPreference = 'Stop'

function Convert-ToLf([string] $text) {
    return $text.Replace("`r`n", "`n")
}

$physicalPath = 'Game/PhysicalPlayground.cpp'
$physical = [System.IO.File]::ReadAllText($physicalPath)

$colorAnchor = Convert-ToLf @'
constexpr std::array<float, 3> M3FogColorRgb{
    M2UnderwaterBackgroundColor.r,
    M2UnderwaterBackgroundColor.g,
    M2UnderwaterBackgroundColor.b};
'@
$colorReplacement = Convert-ToLf @'
constexpr std::array<float, 3> M3FogColorRgb{
    M2UnderwaterBackgroundColor.r,
    M2UnderwaterBackgroundColor.g,
    M2UnderwaterBackgroundColor.b};

// H.4 presentation-only continuation behind the repeated M3 seabed front wall. The colour is scene-linear
// and matched to the accepted wall near its deep fill edge; it is not terrain, collision, bathymetry or
// acoustic authority. A small overlap hides the authored fill-bottom edge even when adjacent X tiles carry
// the accepted -5 m profile continuation step.
constexpr Render::RgbaColor M5ScalableSeabedContinuationColor{0.0034F, 0.0180F, 0.0400F, 1.0F};
constexpr float M5ScalableSeabedContinuationOverlapMeters = 32.0F;
'@
if (-not $physical.Contains($colorAnchor)) { throw 'PhysicalPlayground H4 colour anchor not found' }
$physical = $physical.Replace($colorAnchor, $colorReplacement)

$tileAnchor = Convert-ToLf @'
            if (!terrainTiles)
            {
                return std::unexpected("physical playground scalable environment tiling failed: " +
                                       terrainTiles.error());
            }

            scalableSeabedDraws = BuildEnvironmentPresentationDraws(
'@
$tileReplacement = Convert-ToLf @'
            if (!terrainTiles)
            {
                return std::unexpected("physical playground scalable environment tiling failed: " +
                                       terrainTiles.error());
            }
            if (terrainTiles->empty())
            {
                return std::unexpected("physical playground scalable environment produced no visible terrain tiles");
            }

            // The canonical M3 cross-section has a finite fillBottom because its original 600 m camera never
            // exposed anything below it. Wide M5 framing can expose that implementation edge. Paint only the
            // screen-space region behind the repeated wall, starting slightly above the highest translated
            // fill bottom; the real tiled geometry then overdraws this overlap. This removes the lower box edge
            // without extending render geometry or creating another environment/physics representation.
            float highestTiledFillBottomYMeters = (std::numeric_limits<float>::lowest)();
            for (const EnvironmentPresentationTile& tile : *terrainTiles)
            {
                highestTiledFillBottomYMeters = (std::max)(
                    highestTiledFillBottomYMeters,
                    seabedSection_->renderGeometry.bounds.minimum.y + tile.offsetYMeters);
            }
            const auto seabedContinuationRegion = UnderwaterRegionForSurface(
                *camera,
                highestTiledFillBottomYMeters + M5ScalableSeabedContinuationOverlapMeters);
            if (!seabedContinuationRegion)
            {
                return std::unexpected("physical playground scalable seabed continuation projection failed: " +
                                       seabedContinuationRegion.error());
            }
            if (seabedContinuationRegion->has_value())
            {
                const auto continued = renderer.ClearViewportRect(
                    **seabedContinuationRegion,
                    M5ScalableSeabedContinuationColor);
                if (!continued)
                {
                    return std::unexpected("physical playground scalable seabed continuation clear failed: " +
                                           continued.error());
                }
            }

            scalableSeabedDraws = BuildEnvironmentPresentationDraws(
'@
if (-not $physical.Contains($tileAnchor)) { throw 'PhysicalPlayground H4 terrain tile anchor not found' }
$physical = $physical.Replace($tileAnchor, $tileReplacement)
[System.IO.File]::WriteAllText($physicalPath, $physical, [System.Text.UTF8Encoding]::new($false))

$acceptancePath = 'Game/Combat/CombatPlaygroundAcceptance.h'
$acceptance = [System.IO.File]::ReadAllText($acceptancePath)
$acceptanceAnchor = Convert-ToLf @'
        // Select the initial checkpoint at render time. Startup can spend enough wall time loading the
        // production scene for several fixed ticks to execute before the first present; choosing here keeps
        // the JSON state and the pixels from the same live frame while still requiring no impact and intact
        // destroyer presentation.
        if (!pending_.has_value() && !initialSeen_ && !latestFixedSnapshot_.hasImpact &&
            latestFixedSnapshot_.destroyerIntegrity >= 99.999F)
'@
$acceptanceReplacement = Convert-ToLf @'
        // WindowFrameCapture reads the most recently presented client image, while this callback runs before
        // the current Present. The first render therefore has no valid D3D12 frame for PrintWindow/BitBlt yet.
        // Warm up exactly one render observation, then select Initial from the next live render so state and
        // captured pixels remain synchronized without accepting the compositor's startup-white client area.
        const bool presentedFrameAvailableForCapture = renderWarmupObserved_;
        renderWarmupObserved_ = true;
        if (presentedFrameAvailableForCapture && !pending_.has_value() && !initialSeen_ &&
            !latestFixedSnapshot_.hasImpact && latestFixedSnapshot_.destroyerIntegrity >= 99.999F)
'@
if (-not $acceptance.Contains($acceptanceAnchor)) { throw 'Combat acceptance initial checkpoint anchor not found' }
$acceptance = $acceptance.Replace($acceptanceAnchor, $acceptanceReplacement)

$fieldAnchor = Convert-ToLf @'
    bool gradualAscentObserved_ = false;
};
'@
$fieldReplacement = Convert-ToLf @'
    bool gradualAscentObserved_ = false;
    bool renderWarmupObserved_ = false;
};
'@
if (-not $acceptance.Contains($fieldAnchor)) { throw 'Combat acceptance field anchor not found' }
$acceptance = $acceptance.Replace($fieldAnchor, $fieldReplacement)
[System.IO.File]::WriteAllText($acceptancePath, $acceptance, [System.Text.UTF8Encoding]::new($false))

$mainPath = 'DeepRun/Main.cpp'
$main = [System.IO.File]::ReadAllText($mainPath)
$main = $main.Replace('!IsAllBlack(bits, width, height)', '!IsBlankFrame(bits, width, height)')
$blankAnchor = Convert-ToLf @'
    static bool IsAllBlack(const void* bits, const std::uint32_t width, const std::uint32_t height) noexcept
    {
        const auto* pixels = static_cast<const std::uint8_t*>(bits);
        for (std::uint32_t y = 0; y < height; ++y)
        {
            for (std::uint32_t x = 0; x < width; ++x)
            {
                const std::size_t offset = (static_cast<std::size_t>(y) * width + x) * 4U;
                if (pixels[offset] > 8 || pixels[offset + 1] > 8 || pixels[offset + 2] > 8)
                {
                    return false;
                }
            }
        }
        return true;
    }
'@
$blankReplacement = Convert-ToLf @'
    static bool IsBlankFrame(const void* bits, const std::uint32_t width, const std::uint32_t height) noexcept
    {
        const auto* pixels = static_cast<const std::uint8_t*>(bits);
        bool allBlack = true;
        bool allWhite = true;
        for (std::uint32_t y = 0; y < height; ++y)
        {
            for (std::uint32_t x = 0; x < width; ++x)
            {
                const std::size_t offset = (static_cast<std::size_t>(y) * width + x) * 4U;
                const bool blackPixel = pixels[offset] <= 8 && pixels[offset + 1] <= 8 && pixels[offset + 2] <= 8;
                const bool whitePixel = pixels[offset] >= 247 && pixels[offset + 1] >= 247 && pixels[offset + 2] >= 247;
                allBlack = allBlack && blackPixel;
                allWhite = allWhite && whitePixel;
                if (!allBlack && !allWhite)
                {
                    return false;
                }
            }
        }
        return allBlack || allWhite;
    }
'@
if (-not $main.Contains($blankAnchor)) { throw 'Main frame-blank detector anchor not found' }
$main = $main.Replace($blankAnchor, $blankReplacement)
[System.IO.File]::WriteAllText($mainPath, $main, [System.Text.UTF8Encoding]::new($false))

git config user.name 'Deep Run CI'
git config user.email 'actions@users.noreply.github.com'
git add Game/PhysicalPlayground.cpp Game/Combat/CombatPlaygroundAcceptance.h DeepRun/Main.cpp
git rm Tools/CI/apply_h4_visual_acceptance_fix.ps1 .github/workflows/h4-visual-acceptance-apply.yml
git commit -m 'fix: complete H4 visual acceptance presentation'
git push origin HEAD:tmp/m5-h4-scalable-environment
