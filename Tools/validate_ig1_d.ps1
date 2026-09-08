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

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Push-Location $repoRoot
try {
    Invoke-Checked "IG1-D diff check" {
        git diff --check "$BaseRef...HEAD"
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
    Write-Host "Base ref: $BaseRef"
    Write-Host "Validated: diff, canonical/staged LOD metadata, Debug/Release builds, CTest, headless smoke, windowed/resize smoke."
}
finally {
    Pop-Location
}
