cmake_minimum_required(VERSION 3.28)

project(DeepRun VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "Milestone 0 supports Windows x86-64 only")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CompilerOptions)
include(Dependencies)

file(GLOB DEEPRUN_DXC_CANDIDATES
    "C:/Program Files (x86)/Windows Kits/10/bin/*/x64/dxc.exe"
)
list(SORT DEEPRUN_DXC_CANDIDATES COMPARE NATURAL ORDER DESCENDING)
if(NOT DEEPRUN_DXC_CANDIDATES)
    message(FATAL_ERROR "DXC was not found in the installed Windows SDK")
endif()
list(GET DEEPRUN_DXC_CANDIDATES 0 DEEPRUN_DXC_EXECUTABLE)

set(DEEPRUN_COMPILED_SHADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/CompiledShaders")
set(DEEPRUN_MODEL_VERTEX_SHADER "${DEEPRUN_COMPILED_SHADER_DIR}/ModelVS.cso")
set(DEEPRUN_MODEL_PIXEL_SHADER "${DEEPRUN_COMPILED_SHADER_DIR}/ModelPS.cso")
set(DEEPRUN_SUSPENDED_PARTICLE_VERTEX_SHADER "${DEEPRUN_COMPILED_SHADER_DIR}/SuspendedParticlesVS.cso")
set(DEEPRUN_SUSPENDED_PARTICLE_PIXEL_SHADER "${DEEPRUN_COMPILED_SHADER_DIR}/SuspendedParticlesPS.cso")
set(DEEPRUN_GERSTNER_SURFACE_VERTEX_SHADER "${DEEPRUN_COMPILED_SHADER_DIR}/GerstnerSurfaceVS.cso")
set(DEEPRUN_GERSTNER_SURFACE_PIXEL_SHADER "${DEEPRUN_COMPILED_SHADER_DIR}/GerstnerSurfacePS.cso")
set(DEEPRUN_TONE_MAP_VERTEX_SHADER "${DEEPRUN_COMPILED_SHADER_DIR}/ToneMapVS.cso")
set(DEEPRUN_TONE_MAP_PIXEL_SHADER "${DEEPRUN_COMPILED_SHADER_DIR}/ToneMapPS.cso")
set(DEEPRUN_TONE_MAP_HDR_PIXEL_SHADER "${DEEPRUN_COMPILED_SHADER_DIR}/ToneMapHdrPS.cso")
add_custom_command(
    OUTPUT "${DEEPRUN_MODEL_VERTEX_SHADER}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${DEEPRUN_COMPILED_SHADER_DIR}"
    COMMAND "${DEEPRUN_DXC_EXECUTABLE}"
        -T vs_6_0 -E VSMain -Fo "${DEEPRUN_MODEL_VERTEX_SHADER}"
        "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/Model.hlsl"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/Model.hlsl"
    VERBATIM
)
add_custom_command(
    OUTPUT "${DEEPRUN_MODEL_PIXEL_SHADER}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${DEEPRUN_COMPILED_SHADER_DIR}"
    COMMAND "${DEEPRUN_DXC_EXECUTABLE}"
        -T ps_6_0 -E PSMain -Fo "${DEEPRUN_MODEL_PIXEL_SHADER}"
        "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/Model.hlsl"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/Model.hlsl"
    VERBATIM
)
add_custom_command(
    OUTPUT "${DEEPRUN_SUSPENDED_PARTICLE_VERTEX_SHADER}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${DEEPRUN_COMPILED_SHADER_DIR}"
    COMMAND "${DEEPRUN_DXC_EXECUTABLE}"
        -T vs_6_0 -E VSMain -Fo "${DEEPRUN_SUSPENDED_PARTICLE_VERTEX_SHADER}"
        "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/Particles/UnderwaterParticles.hlsl"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/Particles/UnderwaterParticles.hlsl"
    VERBATIM
)
add_custom_command(
    OUTPUT "${DEEPRUN_SUSPENDED_PARTICLE_PIXEL_SHADER}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${DEEPRUN_COMPILED_SHADER_DIR}"
    COMMAND "${DEEPRUN_DXC_EXECUTABLE}"
        -T ps_6_0 -E PSMain -Fo "${DEEPRUN_SUSPENDED_PARTICLE_PIXEL_SHADER}"
        "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/Particles/UnderwaterParticles.hlsl"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/Particles/UnderwaterParticles.hlsl"
    VERBATIM
)
add_custom_command(
    OUTPUT "${DEEPRUN_GERSTNER_SURFACE_VERTEX_SHADER}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${DEEPRUN_COMPILED_SHADER_DIR}"
    COMMAND "${DEEPRUN_DXC_EXECUTABLE}"
        -T vs_6_0 -E VSMain -Fo "${DEEPRUN_GERSTNER_SURFACE_VERTEX_SHADER}"
        "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/GerstnerSurface.hlsl"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/GerstnerSurface.hlsl"
    VERBATIM
)
add_custom_command(
    OUTPUT "${DEEPRUN_GERSTNER_SURFACE_PIXEL_SHADER}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${DEEPRUN_COMPILED_SHADER_DIR}"
    COMMAND "${DEEPRUN_DXC_EXECUTABLE}"
        -T ps_6_0 -E PSMain -Fo "${DEEPRUN_GERSTNER_SURFACE_PIXEL_SHADER}"
        "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/GerstnerSurface.hlsl"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/GerstnerSurface.hlsl"
    VERBATIM
)
add_custom_command(
    OUTPUT "${DEEPRUN_TONE_MAP_VERTEX_SHADER}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${DEEPRUN_COMPILED_SHADER_DIR}"
    COMMAND "${DEEPRUN_DXC_EXECUTABLE}"
        -T vs_6_0 -E VSMain -Fo "${DEEPRUN_TONE_MAP_VERTEX_SHADER}"
        "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/PostProcess/ToneMap.hlsl"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/PostProcess/ToneMap.hlsl"
    VERBATIM
)
add_custom_command(
    OUTPUT "${DEEPRUN_TONE_MAP_PIXEL_SHADER}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${DEEPRUN_COMPILED_SHADER_DIR}"
    COMMAND "${DEEPRUN_DXC_EXECUTABLE}"
        -T ps_6_0 -E PSMain -Fo "${DEEPRUN_TONE_MAP_PIXEL_SHADER}"
        "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/PostProcess/ToneMap.hlsl"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/PostProcess/ToneMap.hlsl"
    VERBATIM
)
add_custom_command(
    OUTPUT "${DEEPRUN_TONE_MAP_HDR_PIXEL_SHADER}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${DEEPRUN_COMPILED_SHADER_DIR}"
    COMMAND "${DEEPRUN_DXC_EXECUTABLE}"
        -T ps_6_0 -E PSHdrScRgb -Fo "${DEEPRUN_TONE_MAP_HDR_PIXEL_SHADER}"
        "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/PostProcess/ToneMap.hlsl"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/PostProcess/ToneMap.hlsl"
    VERBATIM
)
add_custom_target(DeepRunShaders ALL
    DEPENDS
        "${DEEPRUN_MODEL_VERTEX_SHADER}"
        "${DEEPRUN_MODEL_PIXEL_SHADER}"
        "${DEEPRUN_SUSPENDED_PARTICLE_VERTEX_SHADER}"
        "${DEEPRUN_SUSPENDED_PARTICLE_PIXEL_SHADER}"
        "${DEEPRUN_GERSTNER_SURFACE_VERTEX_SHADER}"
        "${DEEPRUN_GERSTNER_SURFACE_PIXEL_SHADER}"
        "${DEEPRUN_TONE_MAP_VERTEX_SHADER}"
        "${DEEPRUN_TONE_MAP_PIXEL_SHADER}"
        "${DEEPRUN_TONE_MAP_HDR_PIXEL_SHADER}"
)

set(DEEPRUN_ANTEY_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Content/submarines/Antey")
set(DEEPRUN_ANTEY_RUNTIME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Engine/Assets/submarines/Antey")
set(DEEPRUN_ANTEY_RUNTIME_FILES
    "${DEEPRUN_ANTEY_RUNTIME_DIR}/Antey.glb"
    "${DEEPRUN_ANTEY_RUNTIME_DIR}/Antey.asset.json"
    "${DEEPRUN_ANTEY_RUNTIME_DIR}/Antey.authoring.json"
)
add_custom_command(
    OUTPUT ${DEEPRUN_ANTEY_RUNTIME_FILES}
    COMMAND ${CMAKE_COMMAND}
        -DDEEPRUN_ANTEY_SOURCE_DIR=${DEEPRUN_ANTEY_SOURCE_DIR}
        -DDEEPRUN_ANTEY_DESTINATION_DIR=${DEEPRUN_ANTEY_RUNTIME_DIR}
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/StageAnteyRuntime.cmake"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/StageAnteyRuntime.cmake"
        "${DEEPRUN_ANTEY_SOURCE_DIR}/Antey.glb"
        "${DEEPRUN_ANTEY_SOURCE_DIR}/Antey.asset.json"
        "${DEEPRUN_ANTEY_SOURCE_DIR}/Antey.authoring.json"
    VERBATIM
)
add_custom_target(DeepRunAnteyRuntimeAssets DEPENDS ${DEEPRUN_ANTEY_RUNTIME_FILES})

add_library(DeepRunEngine STATIC
    Engine/Assets/AssetManager.cpp
    Engine/Assets/GltfModelLoader.cpp
    Engine/Audio/AudioEngine.cpp
    Engine/Core/Application.cpp
    Engine/Core/CoreServices.cpp
    Engine/Core/Engine.cpp
    Engine/Core/EngineConfig.cpp
    Engine/Core/FixedStepAccumulator.cpp
    Engine/Core/Random.cpp
    Engine/Core/Time.cpp
    Engine/Diagnostics/DebugOverlay.cpp
    Engine/Diagnostics/Logger.cpp
    Engine/Input/HapticMixer.cpp
    Engine/Input/InputState.cpp
    Engine/Input/InputSystem.cpp
    Engine/Input/Windows/WindowsGamingInputGamepad.cpp
    Engine/Physics/PhysicsWorld.cpp
    Engine/Platform/Windows/WinPlatform.cpp
    Engine/Platform/Windows/WinWindow.cpp
    Engine/Render/Camera.cpp
    Engine/Render/ClearRect.cpp
    Engine/Render/D3D12Renderer.cpp
    Engine/Render/IndexedGeometry.cpp
    Engine/Render/ModelDraw.cpp
    Engine/Render/DepthLighting.cpp
    Engine/Render/GerstnerSurface.cpp
    Engine/Render/SuspendedParticles.cpp
    Engine/Render/ViewPathFog.cpp
    Engine/Scene/Scene.cpp
    Simulation/Marine/BuoyancySystem.cpp
    Simulation/Marine/ControlSurfaceSystem.cpp
    Simulation/Marine/HydroDragSystem.cpp
    Simulation/Marine/PropulsionSystem.cpp
    Simulation/Marine/WaterBody.cpp
)

target_compile_features(DeepRunEngine PUBLIC cxx_std_23)
target_include_directories(DeepRunEngine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(DeepRunEngine PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    UNICODE
    _UNICODE
    $<$<CONFIG:Debug>:DEEPRUN_DEBUG=1>
)
deeprun_set_project_warnings(DeepRunEngine)

target_link_libraries(DeepRunEngine
    PUBLIC
        EnTT::EnTT
    PRIVATE
        Jolt
        miniaudio
        imgui
        fastgltf::fastgltf
        nlohmann_json::nlohmann_json
        d3d12
        dxgi
        dxguid
        windowsapp
)

add_executable(DeepRun
    DeepRun/Main.cpp
    Game/Environment/EnvironmentSection.cpp
    Game/Environment/UnderwaterFaunaField.cpp
    Game/Environment/UnderwaterFloraField.cpp
    Game/Environment/UnderwaterIceField.cpp
    Game/Haptics/HapticFeedbackSystem.cpp
    Game/PhysicalPlayground.cpp
    Game/PhysicsRenderSync.cpp
    Game/PropulsionPresentation.cpp
    Game/Submarine/ProductionAnteyAsset.cpp
    Game/SurfaceFloatModel.cpp
    Game/Submarine/VesselCommandState.cpp
    Game/WaterPresentation.cpp
)
target_compile_features(DeepRun PRIVATE cxx_std_23)
deeprun_set_project_warnings(DeepRun)
# user32/gdi32 are used only by the M2 Slice C2 window frame capture (PrintWindow/BitBlt) in Main.cpp.
target_link_libraries(DeepRun PRIVATE DeepRunEngine nlohmann_json::nlohmann_json user32 gdi32)
add_custom_target(DeepRunConfig ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:DeepRun>/Config"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/Config/engine.json"
        "$<TARGET_FILE_DIR:DeepRun>/Config/engine.json"
)
add_dependencies(DeepRun DeepRunConfig)
add_dependencies(DeepRun DeepRunShaders)
add_dependencies(DeepRun DeepRunAnteyRuntimeAssets)
add_custom_command(TARGET DeepRun POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:DeepRun>/Content/submarines/prototype"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/Engine/Assets/submarines/prototype/submarine_prototype.glb"
        "$<TARGET_FILE_DIR:DeepRun>/Content/submarines/prototype/submarine_prototype.glb"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${DEEPRUN_ANTEY_RUNTIME_DIR}"
        "$<TARGET_FILE_DIR:DeepRun>/Content/submarines/Antey"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:DeepRun>/Shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${DEEPRUN_MODEL_VERTEX_SHADER}"
        "${DEEPRUN_MODEL_PIXEL_SHADER}"
        "${DEEPRUN_SUSPENDED_PARTICLE_VERTEX_SHADER}"
        "${DEEPRUN_SUSPENDED_PARTICLE_PIXEL_SHADER}"
        "${DEEPRUN_GERSTNER_SURFACE_VERTEX_SHADER}"
        "${DEEPRUN_GERSTNER_SURFACE_PIXEL_SHADER}"
        "${DEEPRUN_TONE_MAP_VERTEX_SHADER}"
        "${DEEPRUN_TONE_MAP_PIXEL_SHADER}"
        "${DEEPRUN_TONE_MAP_HDR_PIXEL_SHADER}"
        "$<TARGET_FILE_DIR:DeepRun>/Shaders"
)

enable_testing()

add_executable(DeepRunTests
    Tests/TestMain.cpp
    Game/Environment/EnvironmentSection.cpp
    Game/Environment/UnderwaterFaunaField.cpp
    Game/Environment/UnderwaterFloraField.cpp
    Game/Environment/UnderwaterIceField.cpp
    Game/Haptics/HapticFeedbackSystem.cpp
    Game/PhysicsRenderSync.cpp
    Game/PropulsionPresentation.cpp
    Game/Submarine/ProductionAnteyAsset.cpp
    Game/SurfaceFloatModel.cpp
    Game/Submarine/VesselCommandState.cpp
    Game/WaterPresentation.cpp
)
target_compile_features(DeepRunTests PRIVATE cxx_std_23)
target_compile_definitions(DeepRunTests PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    DEEPRUN_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
)
deeprun_set_project_warnings(DeepRunTests)
target_link_libraries(DeepRunTests PRIVATE DeepRunEngine nlohmann_json::nlohmann_json)
add_dependencies(DeepRunTests DeepRunAnteyRuntimeAssets)
add_custom_command(TARGET DeepRunTests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:DeepRunTests>/Assets/submarines/prototype"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/Engine/Assets/submarines/prototype/submarine_prototype.glb"
        "$<TARGET_FILE_DIR:DeepRunTests>/Assets/submarines/prototype/submarine_prototype.glb"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${DEEPRUN_ANTEY_RUNTIME_DIR}"
        "$<TARGET_FILE_DIR:DeepRunTests>/Assets/submarines/Antey"
)

add_test(NAME DeepRunTests COMMAND DeepRunTests --asset-root Assets)
set_tests_properties(DeepRunTests PROPERTIES
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:DeepRunTests>"
)

# IG1-D is a bounded presentation-policy test. It has no renderer, physics, authoring-file, or staged-content
# dependency: the test constructs semantic LOD-family definitions directly and verifies deterministic selection.
add_executable(DeepRunIG1DLodPolicyTests
    Tests/IG1DLodPolicyTest.cpp
)
target_compile_features(DeepRunIG1DLodPolicyTests PRIVATE cxx_std_23)
target_compile_definitions(DeepRunIG1DLodPolicyTests PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)
deeprun_set_project_warnings(DeepRunIG1DLodPolicyTests)
target_link_libraries(DeepRunIG1DLodPolicyTests PRIVATE DeepRunEngine)
add_test(NAME DeepRunIG1DLodPolicyTests COMMAND DeepRunIG1DLodPolicyTests)

set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT DeepRun)
