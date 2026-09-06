# Testing DeepRun

Run all commands from the repository root unless a command explicitly changes
directory.

## Configure and build

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
cmake --preset windows-release
cmake --build --preset windows-release
```

## Canonical tests

```powershell
ctest --preset windows-debug
ctest --preset windows-release
```

The registered `DeepRunTests` CTest entry runs:

```text
working directory: build/windows-debug/Debug or build/windows-release/Release
command:           DeepRunTests --asset-root Assets
environment:       no test-specific environment variables
```

`Assets` is relative to the test executable's output directory. CMake copies
the canonical prototype asset to `Assets/submarines/prototype/` there after
each test-target build. CTest therefore reports one registered test (`1/1`),
while `DeepRunTests` reports its individual assertion count (currently
`239/239`).

Do not assume a bare `DeepRunTests.exe` launch is equivalent to CTest. A
supported direct invocation is:

```powershell
Push-Location build/windows-debug/Debug
.\DeepRunTests.exe --asset-root Assets
Pop-Location
```

Launching `build/windows-debug/Debug/DeepRunTests.exe --asset-root Assets`
from the repository root instead searches for `./Assets`, not the copied
build-output assets, and is not an acceptance result.

## Smoke tests

```powershell
.\build\windows-debug\Debug\DeepRun.exe --headless
.\build\windows-release\Release\DeepRun.exe --headless
.\build\windows-debug\Debug\DeepRun.exe --smoke-test
.\build\windows-release\Release\DeepRun.exe --smoke-test
```

`--smoke-test` is the canonical windowed rendering and resize smoke path. In a
Debug build it also enables the existing D3D12 debug validation path; do not
add separate message suppression merely to run this check.

For milestone acceptance, report the exact Debug and Release CTest commands
and their results. Treat registered CTest results—not an ad-hoc direct launch
with different working-directory or asset-root inputs—as the canonical signal.
