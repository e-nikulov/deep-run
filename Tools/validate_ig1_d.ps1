param(
    [string]$BaseRef = "origin/patch",
    [string]$BuildRoot = "build/ig1-d-acceptance"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Label,
        [Parameter(Mandatory = $true)]
        [scriptblock]$Command
    )

    Write-Host ""
    Write-Host "=== $Label ==="
    & $Command
    if ($LASTEXITCODE -ne 0) {
        throw "$Label failed with exit code $LASTEXITCODE"
    }
}

function Test-GitCommitRef {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Ref
    )

    git rev-parse --verify --quiet "$Ref^{commit}" *> $null
    return $LASTEXITCODE -eq 0
}

function Resolve-BaseCommitRef {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RequestedRef
    )

    if (Test-GitCommitRef $RequestedRef) {
        return $RequestedRef
    }

    $remoteRef = "origin/$RequestedRef"
    if (Test-GitCommitRef $remoteRef) {
        Write-Host "Using remote-tracking base ref '$remoteRef' because local '$RequestedRef' is absent."
        return $remoteRef
    }

    Write-Host "Base ref '$RequestedRef' is not present locally; fetching origin/$RequestedRef."
    git fetch origin "+refs/heads/${RequestedRef}:refs/remotes/origin/${RequestedRef}"
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to fetch base branch '$RequestedRef' from origin"
    }
    if (-not (Test-GitCommitRef $remoteRef)) {
        throw "Base branch '$RequestedRef' could not be resolved after fetch"
    }

    return $remoteRef
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$acceptanceBuildRoot = if ([System.IO.Path]::IsPathRooted($BuildRoot)) {
    $BuildRoot
}
else {
    Join-Path $repoRoot $BuildRoot
}
$debugBuildDir = Join-Path $acceptanceBuildRoot "debug"
$releaseBuildDir = Join-Path $acceptanceBuildRoot "release"
Push-Location $repoRoot
try {
    $resolvedBaseRef = Resolve-BaseCommitRef $BaseRef

    Invoke-Checked "IG1-D diff check" {
        git diff --check "$resolvedBaseRef...HEAD"
    }

    Invoke-Checked "IG1-D canonical content LOD validation" {
        python Tools/Blender/validate_antey_runtime_lods.py --package Content/submarines/Antey
    }

    Invoke-Checked "Debug configure" {
        cmake --preset windows-debug -B $debugBuildDir
    }
    Invoke-Checked "Debug build" {
        cmake --build $debugBuildDir --config Debug
    }

    Invoke-Checked "Release configure" {
        cmake --preset windows-release -B $releaseBuildDir
    }
    Invoke-Checked "Release build" {
        cmake --build $releaseBuildDir --config Release
    }

    Invoke-Checked "IG1-D staged runtime LOD validation" {
        python Tools/Blender/validate_antey_runtime_lods.py --package Engine/Assets/submarines/Antey
    }

    Invoke-Checked "Debug CTest" {
        ctest --test-dir $debugBuildDir -C Debug --output-on-failure
    }
    Invoke-Checked "Release CTest" {
        ctest --test-dir $releaseBuildDir -C Release --output-on-failure
    }

    Invoke-Checked "Debug headless smoke" {
        & (Join-Path $debugBuildDir "Debug\DeepRun.exe") --headless
    }
    Invoke-Checked "Release headless smoke" {
        & (Join-Path $releaseBuildDir "Release\DeepRun.exe") --headless
    }

    Invoke-Checked "Debug windowed/resize smoke" {
        & (Join-Path $debugBuildDir "Debug\DeepRun.exe") --smoke-test
    }
    Invoke-Checked "Release windowed/resize smoke" {
        & (Join-Path $releaseBuildDir "Release\DeepRun.exe") --smoke-test
    }

    Write-Host ""
    Write-Host "IG1-D ACCEPTANCE HARNESS: PASS"
    Write-Host "Requested base ref: $BaseRef"
    Write-Host "Resolved base ref: $resolvedBaseRef"
    Write-Host "Build root: $acceptanceBuildRoot"
    Write-Host "Validated: diff, canonical/staged LOD metadata, Debug/Release builds, CTest, headless smoke, windowed/resize smoke."
}
finally {
    Pop-Location
}
