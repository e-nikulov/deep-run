# DeepRun

DeepRun is a Windows submarine roguelite built on a purpose-built C++23 engine. The current implementation is Milestone 1: a modular core with explicit lifecycle and frame state, an EnTT-backed Scene, generic transforms, runtime resource caching, validated JSON configuration, input state, diagnostics, tests, and a headless smoke path. It preserves the Milestone 0 Win32, Direct3D 12, Jolt Physics, miniaudio, and Dear ImGui foundation.

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

Press `F1` to toggle the developer overlay and `Escape` to quit. A controller reachable through XInput is reported in the overlay and log.

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
