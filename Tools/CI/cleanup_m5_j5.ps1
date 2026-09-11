$ErrorActionPreference = 'Stop'
function Read-Lf([string]$Path) { return (Get-Content -Raw $Path).Replace("`r`n", "`n") }
function Write-Lf([string]$Path, [string]$Content) { [System.IO.File]::WriteAllText($Path, $Content, [System.Text.UTF8Encoding]::new($false)) }
function Replace-Once([string]$Path, [string]$Old, [string]$New) {
    $text = Read-Lf $Path
    $Old = $Old.Replace("`r`n", "`n").Trim([char[]]"`r`n")
    $New = $New.Replace("`r`n", "`n").Trim([char[]]"`r`n")
    $first = $text.IndexOf($Old, [System.StringComparison]::Ordinal)
    if ($first -lt 0) { throw "Anchor not found in $Path" }
    if ($text.IndexOf($Old, $first + $Old.Length, [System.StringComparison]::Ordinal) -ge 0) { throw "Anchor not unique in $Path" }
    Write-Lf $Path ($text.Substring(0,$first) + $New + $text.Substring($first+$Old.Length))
}
Replace-Once 'Engine/Input/InputSystem.cpp' @'
    result.cameraPanX = camera.x;
    result.cameraPanY = camera.y;

    // M5-J5 gives LT/RT back to the canonical weapon semantics. In the current tactical-camera context,
'@ @'
    result.cameraPanX = camera.x;

    // M5-J5 gives LT/RT back to the canonical weapon semantics. In the current tactical-camera context,
'@
Replace-Once 'Tests/M5PlayerControlledCombatChecks.h' @'
#include <array>
#include <cmath>
'@ @'
#include <algorithm>
#include <array>
#include <cmath>
'@
Replace-Once 'Game/Combat/CombatPlaygroundRuntime.h' @'
        bool integratedActiveEcho = false;
        if (activePulse_ && activeReflector_)
'@ @'
        if (activePulse_ && activeReflector_)
'@
Replace-Once 'Game/Combat/CombatPlaygroundRuntime.h' @'
                integratedActiveEcho = true;
                integratedPlayerEvidence = true;
'@ @'
                integratedPlayerEvidence = true;
'@
Replace-Once 'Game/Combat/CombatPlaygroundRuntime.h' @'
        if (!integratedPlayerEvidence && !integratedActiveEcho && !playerTracks_.AdvanceTo(simulationTimeSeconds))
'@ @'
        if (!integratedPlayerEvidence && !playerTracks_.AdvanceTo(simulationTimeSeconds))
'@
git diff --check
