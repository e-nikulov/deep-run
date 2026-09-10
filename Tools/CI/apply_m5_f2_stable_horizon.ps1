Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$testPath = 'Tests/M5CombatPlaygroundRuntimeChecks.h'
$text = [System.IO.File]::ReadAllText($testPath).Replace("`r`n", "`n")

$oldHorizon = @'
    constexpr float fixedDeltaSeconds = 1.0F / 60.0F;
    constexpr int finalTick = 2700; // 45 s: enough for active echo, 1.8 km run, impact, and post-impact stability.
'@.Replace("`r`n", "`n")
$newHorizon = @'
    constexpr float fixedDeltaSeconds = 1.0F / 60.0F;
    // F.1 active ranging intentionally delays the destroyer's qualified launch. Give the reciprocal F.2 weapon
    // enough deterministic SimulationTime to traverse the ~1.7 km engagement and prove physical impact/state cleanup.
    constexpr int finalTick = 4800; // 80 s at 60 Hz.
    constexpr double finalSimulationTimeSeconds =
        static_cast<double>(finalTick) * static_cast<double>(fixedDeltaSeconds);
'@.Replace("`r`n", "`n")
if (-not $text.Contains($oldHorizon)) {
    throw 'M5-F2 original 45-second horizon block was not found'
}
$text = $text.Replace($oldHorizon, $newHorizon)

$oldPresentation = 'BuildCombatPlaygroundPresentationSnapshot(runtime, physicsWorld, 45.0)'
$newPresentation = 'BuildCombatPlaygroundPresentationSnapshot(runtime, physicsWorld, finalSimulationTimeSeconds)'
if (-not $text.Contains($oldPresentation)) {
    throw 'M5-F2 final presentation timestamp was not found'
}
$text = $text.Replace($oldPresentation, $newPresentation)

$oldReverse = 'cameraDirector.Evaluate(runtime, 44.0)'
$newReverse = 'cameraDirector.Evaluate(runtime, finalSimulationTimeSeconds - 1.0)'
if (-not $text.Contains($oldReverse)) {
    throw 'M5-F2 camera time-reversal probe was not found'
}
$text = $text.Replace($oldReverse, $newReverse)

[System.IO.File]::WriteAllText($testPath, $text, [System.Text.UTF8Encoding]::new($false))

Remove-Item -Force 'Tools/CI/apply_m5_f2_stable_horizon.ps1'
Remove-Item -Force '.github/workflows/m5-f2-stable-horizon.yml'

Write-Host 'M5-F2 stable 80-second regression horizon applied; staging files removed.'
