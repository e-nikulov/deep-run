Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$path = 'Tests/M5CombatPlaygroundRuntimeChecks.h'
$text = [System.IO.File]::ReadAllText($path).Replace("`r`n", "`n")
if ($text.Contains('[M5-F.2 diagnostic]')) {
    throw 'diagnostics already applied'
}

# Probe whether the reciprocal torpedo simply needs the physically implied travel time after late fire-control launch.
$text = $text.Replace('constexpr int finalTick = 2700;', 'constexpr int finalTick = 4800;')
$text = $text.Replace('BuildCombatPlaygroundPresentationSnapshot(runtime, physicsWorld, 45.0)', 'BuildCombatPlaygroundPresentationSnapshot(runtime, physicsWorld, 80.0)')

# Instrument the original returns first so the helper itself remains a normal false-returning lambda.
$text = $text.Replace('return false;', 'return fail(__LINE__);')
$text = $text.Replace("#include <cmath>`n", "#include <cmath>`n#include <iostream>`n")
$needle = '    using Game::Combat::CombatPlaygroundCameraMode;'
$helper = @'
    const auto fail = [](const int line) {
        std::cerr << "[M5-F.2 diagnostic] failure line " << line << '\n';
        return false;
    };
    using Game::Combat::CombatPlaygroundCameraMode;
'@.Replace("`r`n", "`n")
if (-not $text.Contains($needle)) {
    throw 'diagnostic insertion point not found'
}
$text = $text.Replace($needle, $helper)

$summaryNeedle = '    const auto destroyerState = physicsWorld.GetBodyState(runtime.Destroyer().body);'
$summaryBlock = @'
    const auto destroyerState = physicsWorld.GetBodyState(runtime.Destroyer().body);
    std::cerr << "[M5-F.2 summary]"
              << " spatial=" << sawDestroyerSpatialFireControlTrack
              << " launch=" << sawDestroyerLaunch
              << " materialized=" << sawDestroyerTorpedoMaterialized
              << " no_identity=" << sawDestroyerTorpedoUnderwaterWithoutBodyIdentity
              << " impact=" << sawDestroyerTorpedoImpact
              << " hidden=" << sawDestroyerTorpedoHiddenAfterImpact
              << " present_draw=" << sawPresentationDestroyerTorpedo
              << " mine_draw=" << sawPresentationMine
              << " player_impact=" << sawImpact
              << " decoy_diversion=" << sawDecoyDiversion
              << " decoy_recovery=" << sawPostDecoyRecovery
              << " gradual_ascent=" << sawGradualAscent
              << " tactical=" << sawTacticalCamera
              << " stable_ticks=" << stableTacticalTicks;
    if (runtime.DestroyerTorpedo())
    {
        std::cerr << " enemy_domain=" << static_cast<int>(runtime.DestroyerTorpedo()->movementDomain)
                  << " enemy_x=" << runtime.DestroyerTorpedo()->positionMeters.x
                  << " enemy_y=" << runtime.DestroyerTorpedo()->positionMeters.y
                  << " enemy_impacted=" << runtime.DestroyerTorpedo()->impactedBody.has_value();
    }
    else
    {
        std::cerr << " enemy_domain=-1";
    }
    std::cerr << " player_integrity="
              << (runtime.PlayerIntegrity() ? runtime.PlayerIntegrity()->remainingIntegrity : -1.0F)
              << " destroyer_integrity=" << runtime.Destroyer().integrity.remainingIntegrity
              << " mine_detonated=" << (runtime.Mine() ? runtime.Mine()->detonated : true)
              << '\n';
'@.Replace("`r`n", "`n")
if (-not $text.Contains($summaryNeedle)) {
    throw 'summary insertion point not found'
}
$text = $text.Replace($summaryNeedle, $summaryBlock)

[System.IO.File]::WriteAllText($path, $text, [System.Text.UTF8Encoding]::new($false))

cmake --preset windows-debug
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
cmake --build --preset windows-debug --target DeepRunM5WeaponRuntimeTests --parallel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& .\build\windows-debug\Debug\DeepRunM5WeaponRuntimeTests.exe
exit $LASTEXITCODE
