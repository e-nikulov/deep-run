# DeepRun

DeepRun is a Windows submarine roguelite built on a purpose-built C++23 engine. Milestones 0 — Engine Bootstrap, 1 — Core Engine, and 2 — Physical Playground are complete. The next implementation milestone is Milestone 3 — Underwater Environment. The completed foundation provides the Win32, Direct3D 12, Jolt Physics, miniaudio, Dear ImGui, asset, input, fixed-step simulation, marine-physics, controller-control, and headless-test paths required by the accepted M0-M2 scope.

## Prerequisites

- Visual Studio 2026 Community with Desktop development with C++
- Windows 11 SDK `10.0.26100.0` or newer
- CMake 3.28 or newer (the Visual Studio bundled CMake is supported)
- Git, used by CMake to acquire pinned dependencies during the first configure

## Configure and build

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
```

For a release build:

```powershell
cmake --preset windows-release
cmake --build --preset windows-release
```

Jolt Physics, miniaudio, Dear ImGui, EnTT, and nlohmann/json are fetched at pinned Git tags into the CMake build directory. No manual dependency downloads are required. `Config/engine.json` is copied next to the executable after each build.

## Run

```powershell
./build/windows-debug/Debug/DeepRun.exe
```

Press `F1` to toggle the developer overlay and `Escape` to quit. A controller reachable through the Windows.Gaming.Input backend is reported in the overlay and log.

Headless physics smoke test:

```powershell
./build/windows-debug/Debug/DeepRun.exe --headless
```

Automated window, rendering, and resize smoke run:

```powershell
./build/windows-debug/Debug/DeepRun.exe --smoke-test
```

## Tests

```powershell
ctest --preset windows-debug
ctest --preset windows-release
```

Headless mode deliberately skips window, renderer, Dear ImGui, input hardware polling, and audio device initialization. It loads and validates engine configuration, initializes the core Scene/resource services and Jolt, runs a deterministic rigid-body simulation, and returns a non-zero exit code on failure.
