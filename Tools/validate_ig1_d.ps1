param(
    [string]$BaseRef = "patch"
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
        cmake --preset windows-debug
    }
    Invoke-Checked "Debug build" {
        cmake --build --preset windows-debug
    }

    Invoke-Checked "Release configure" {
        cmake --preset windows-release
    }
    Invoke-Checked "Release build" {
        cmake --build --preset windows-release
    }

    Invoke-Checked "IG1-D staged runtime LOD validation" {
        python Tools/Blender/validate_antey_runtime_lods.py --package Engine/Assets/submarines/Antey
    }

    Invoke-Checked "Debug CTest" {
        ctest --preset windows-debug --output-on-failure
    }
    Invoke-Checked "Release CTest" {
        ctest --preset windows-release --output-on-failure
    }

    Invoke-Checked "Debug headless smoke" {
        .\build\windows-debug\Debug\DeepRun.exe --headless
    }
    Invoke-Checked "Release headless smoke" {
        .\build\windows-release\Release\DeepRun.exe --headless
    }

    Invoke-Checked "Debug windowed/resize smoke" {
        .\build\windows-debug\Debug\DeepRun.exe --smoke-test
    }
    Invoke-Checked "Release windowed/resize smoke" {
        .\build\windows-release\Release\DeepRun.exe --smoke-test
    }

    Write-Host ""
    Write-Host "IG1-D ACCEPTANCE HARNESS: PASS"
    Write-Host "Requested base ref: $BaseRef"
    Write-Host "Resolved base ref: $resolvedBaseRef"
    Write-Host "Validated: diff, canonical/staged LOD metadata, Debug/Release builds, CTest, headless smoke, windowed/resize smoke."
}
finally {
    Pop-Location
}
