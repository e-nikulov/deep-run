$ErrorActionPreference = 'Stop'

$physicalPath = 'Game/PhysicalPlayground.cpp'
$physical = [System.IO.File]::ReadAllText($physicalPath)

if (-not $physical.Contains('#include "Game/Environment/ScalableEnvironmentPresentation.h"')) {
    $includePattern = '#include "Game/Environment/UnderwaterFaunaField\.h"\r?\n#include "Game/Environment/UnderwaterFloraField\.h"\r?\n#include "Game/Environment/UnderwaterIceField\.h"'
    $includeReplacement = @'
#include "Game/Environment/ScalableEnvironmentPresentation.h"
#include "Game/Environment/UnderwaterFaunaField.h"
#include "Game/Environment/UnderwaterFloraField.h"
#include "Game/Environment/UnderwaterIceField.h"
'@
    $includeRegex = [regex]::new($includePattern)
    if (-not $includeRegex.IsMatch($physical)) { throw 'PhysicalPlayground include anchor not found' }
    $physical = $includeRegex.Replace($physical, $includeReplacement, 1)
}

$renderPattern = '    const auto gerstnerStats = renderer\.DrawGerstnerSurface\(\*camera, simulationTimeSeconds\);.*?(?=    // Physics diagnostics live in FixedUpdate\.)'
$renderRegex = [regex]::new($renderPattern, [System.Text.RegularExpressions.RegexOptions]::Singleline)
if (-not $renderRegex.IsMatch($physical)) { throw 'PhysicalPlayground render replacement anchors not found' }

$newRenderBlock = @'
    // M5-H.4 scalable presentation: the accepted M3 section remains the sole local environment/physics
    // authority. Wider tactical views reuse only presentation draw instances. No additional Jolt bodies,
    // WaterBody state, acoustic terrain, navigation authority or gameplay objects are created here.
    std::span<const Render::ModelDrawInstance> seabedPresentationDraws{seabedDraws_};
    std::span<const Render::ModelDrawInstance> floraPresentationDraws{floraDraws_};
    std::span<const Render::ModelDrawInstance> icePresentationDraws{iceDraws_};
    Render::ModelDrawInstance faunaDraw = faunaBaseDraw_;
    faunaDraw.modelToWorld = *faunaModelToWorld;
    std::span<const Render::ModelDrawInstance> faunaPresentationDraws(&faunaDraw, 1U);

    std::vector<Render::ModelDrawInstance> scalableSeabedDraws;
    std::vector<Render::ModelDrawInstance> scalableFloraDraws;
    std::vector<Render::ModelDrawInstance> scalableIceDraws;
    std::vector<Render::ModelDrawInstance> scalableFaunaDraws;

    if (freePresentationCameraFraming_)
    {
        if (UseDetailedEnvironmentPresentation(camera->width))
        {
            // The canonical section ends five metres lower on the east edge than on the west edge. Applying
            // the same -5 m step to repeated seabed/flora tiles joins adjacent profile endpoints exactly,
            // avoiding the vertical walls/seams that exposed the old M3 rectangle in wide views.
            constexpr float SeabedTileVerticalStepMeters = -5.0F;
            const auto terrainTiles = BuildEnvironmentPresentationTiles(
                seabedSection_->renderGeometry.bounds.minimum.x,
                seabedSection_->renderGeometry.bounds.maximum.x,
                camera->target.x,
                camera->width,
                SeabedTileVerticalStepMeters);
            if (!terrainTiles)
            {
                return std::unexpected("physical playground scalable environment tiling failed: " +
                                       terrainTiles.error());
            }

            scalableSeabedDraws = BuildEnvironmentPresentationDraws(
                std::span<const Render::ModelDrawInstance>(seabedDraws_),
                std::span<const EnvironmentPresentationTile>(*terrainTiles));
            scalableFloraDraws = BuildEnvironmentPresentationDraws(
                std::span<const Render::ModelDrawInstance>(floraDraws_),
                std::span<const EnvironmentPresentationTile>(*terrainTiles));
            seabedPresentationDraws = scalableSeabedDraws;
            floraPresentationDraws = scalableFloraDraws;

            // Upper-water ice and the fish school remain tied to sea/depth presentation rather than the
            // sloping seabed continuation, so only X repeats; Y is deliberately unchanged.
            std::vector<EnvironmentPresentationTile> waterColumnTiles = *terrainTiles;
            for (EnvironmentPresentationTile& tile : waterColumnTiles)
            {
                tile.offsetYMeters = 0.0F;
            }
            scalableIceDraws = BuildEnvironmentPresentationDraws(
                std::span<const Render::ModelDrawInstance>(iceDraws_),
                std::span<const EnvironmentPresentationTile>(waterColumnTiles));
            scalableFaunaDraws = BuildEnvironmentPresentationDraws(
                std::span<const Render::ModelDrawInstance>(&faunaDraw, 1U),
                std::span<const EnvironmentPresentationTile>(waterColumnTiles));
            icePresentationDraws = scalableIceDraws;
            faunaPresentationDraws = scalableFaunaDraws;
        }
        else
        {
            // Operational/strategic bands intentionally do not multiply local M3 decoration. Higher-level
            // contact/symbol presentation owns those scales; rendering a tiny bounded terrain tile would
            // recreate the exact rectangular artifact H.4 removes.
            seabedPresentationDraws = {};
            floraPresentationDraws = {};
            icePresentationDraws = {};
            faunaPresentationDraws = {};
        }
    }

    // The M3 Gerstner surface and suspended-particle field are authored as bounded local-detail envelopes.
    // Draw them only while that envelope fully covers the viewport. At wider/panned framing the full-width
    // WaterBody-derived underlay + depth/fog presentation remains, so no differently shaded rectangle can
    // reveal the local mesh/field bounds. Untouched 600 m M3 still takes the original draw path exactly once.
    const Render::GerstnerSurfacePresentationParameters gerstnerPresentation =
        BuildGerstnerSurfacePresentation(*water_);
    const bool gerstnerCoversView = HorizontalPresentationBoundsCoverView(
        gerstnerPresentation.minimumX,
        gerstnerPresentation.maximumX,
        camera->target.x,
        camera->width);
    std::expected<Render::GerstnerSurfaceDrawStats, std::string> gerstnerStats =
        Render::GerstnerSurfaceDrawStats{};
    if (gerstnerCoversView)
    {
        gerstnerStats = renderer.DrawGerstnerSurface(*camera, simulationTimeSeconds);
    }
    if (!gerstnerStats)
    {
        return std::unexpected("physical playground Gerstner surface draw failed: " + gerstnerStats.error());
    }

    std::expected<Render::ModelDrawStats, std::string> seabedStats = Render::ModelDrawStats{};
    if (!seabedPresentationDraws.empty())
    {
        seabedStats = renderer.DrawModel(seabedModel_, seabedPresentationDraws, *camera);
    }
    if (!seabedStats)
    {
        return seabedStats;
    }

    std::expected<Render::ModelDrawStats, std::string> floraStats = Render::ModelDrawStats{};
    if (!floraPresentationDraws.empty())
    {
        floraStats = renderer.DrawModel(floraModel_, floraPresentationDraws, *camera);
    }
    if (!floraStats)
    {
        return floraStats;
    }

    std::expected<Render::ModelDrawStats, std::string> iceStats = Render::ModelDrawStats{};
    if (!icePresentationDraws.empty())
    {
        iceStats = renderer.DrawModel(iceModel_, icePresentationDraws, *camera);
    }
    if (!iceStats)
    {
        return iceStats;
    }

    std::expected<Render::ModelDrawStats, std::string> faunaStats = Render::ModelDrawStats{};
    if (!faunaPresentationDraws.empty())
    {
        faunaStats = renderer.DrawModel(faunaModel_, faunaPresentationDraws, *camera);
    }
    if (!faunaStats)
    {
        return faunaStats;
    }

    const auto submarineStats = renderer.DrawModel(submarineModel_, *draws, *camera);
    if (!submarineStats)
    {
        return submarineStats;
    }
    const auto surfaceFloatStats = renderer.DrawModel(
        surfaceFloatModelGpu_, std::span<const Render::ModelDrawInstance>(&surfaceFloatDraw, 1U), *camera);
    if (!surfaceFloatStats)
    {
        return surfaceFloatStats;
    }

    const bool particleFieldCoversView = HorizontalPresentationBoundsCoverView(
        M3UnderwaterParticleField.minimumWorldPosition[0],
        M3UnderwaterParticleField.maximumWorldPosition[0],
        camera->target.x,
        camera->width);
    std::expected<Render::SuspendedParticleDrawStats, std::string> particleStats =
        Render::SuspendedParticleDrawStats{};
    if (particleFieldCoversView)
    {
        particleStats = renderer.DrawSuspendedParticleField(*camera);
    }
    if (!particleStats)
    {
        return std::unexpected("physical playground particle draw failed: " + particleStats.error());
    }

'@
$physical = $renderRegex.Replace($physical, $newRenderBlock, 1)
[System.IO.File]::WriteAllText($physicalPath, $physical, [System.Text.UTF8Encoding]::new($false))

$mainPath = 'DeepRun/Main.cpp'
$main = [System.IO.File]::ReadAllText($mainPath)
$mainPattern = '                \+\+renderFrames;\r?\n                return rendered->drawCalls == 74 && rendered->submittedPrimitives == 72 &&\r?\n                       rendered->submittedIndices == 364380;'
$mainRegex = [regex]::new($mainPattern)
if (-not $mainRegex.IsMatch($main)) { throw 'Main M3 draw-stat gate anchor not found' }
$newMainGate = @'
                ++renderFrames;
                if (options.benchmarkM3)
                {
                    // Historical M3 benchmark remains an exact regression gate. Normal M5 framing may
                    // intentionally add presentation-only environment tiles or suppress bounded local-detail
                    // passes, so applying M3 draw totals to it would incorrectly make valid zoom/pan fail.
                    return rendered->drawCalls == 74 && rendered->submittedPrimitives == 72 &&
                           rendered->submittedIndices == 364380;
                }
                return rendered->drawCalls > 0U && rendered->submittedPrimitives > 0U &&
                       rendered->submittedIndices > 0U;
'@
$main = $mainRegex.Replace($main, $newMainGate, 1)
[System.IO.File]::WriteAllText($mainPath, $main, [System.Text.UTF8Encoding]::new($false))

git config user.name 'Deep Run CI'
git config user.email 'actions@users.noreply.github.com'
git add Game/PhysicalPlayground.cpp DeepRun/Main.cpp
git rm Tools/CI/apply_h4_render_integration.ps1 .github/workflows/h4-apply.yml
git commit -m 'fix: integrate scalable M5 environment rendering'
git push origin HEAD:tmp/m5-h4-scalable-environment
