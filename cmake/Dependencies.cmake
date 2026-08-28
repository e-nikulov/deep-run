include(FetchContent)

set(FETCHCONTENT_QUIET OFF)

set(JPH_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(JPH_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(JPH_BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(JPH_BUILD_HELLO_WORLD OFF CACHE BOOL "" FORCE)
set(JPH_BUILD_VIEWER OFF CACHE BOOL "" FORCE)
set(JPH_BUILD_ASSET_CONVERTER OFF CACHE BOOL "" FORCE)
set(JPH_BUILD_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
set(JPH_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(JPH_ENABLE_ASSERTS ON CACHE BOOL "" FORCE)
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    JoltPhysics
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG v5.5.0
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR Build
)

FetchContent_Declare(
    miniaudio_source
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG 0.11.23
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR deeprun-no-cmake
)

FetchContent_Declare(
    imgui_source
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.9b
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR deeprun-no-cmake
)

FetchContent_Declare(
    EnTT
    GIT_REPOSITORY https://github.com/skypjack/entt.git
    GIT_TAG v3.16.0
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.12.0
    GIT_SHALLOW TRUE
)

set(FASTGLTF_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(FASTGLTF_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FASTGLTF_ENABLE_DOCS OFF CACHE BOOL "" FORCE)
set(FASTGLTF_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(FASTGLTF_ENABLE_CPP_MODULES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    fastgltf
    GIT_REPOSITORY https://github.com/spnda/fastgltf.git
    GIT_TAG v0.9.0
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(JoltPhysics miniaudio_source imgui_source EnTT nlohmann_json fastgltf)

add_library(miniaudio INTERFACE)
target_include_directories(miniaudio SYSTEM INTERFACE "${miniaudio_source_SOURCE_DIR}")

add_library(imgui STATIC
    "${imgui_source_SOURCE_DIR}/imgui.cpp"
    "${imgui_source_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_source_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_source_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_source_SOURCE_DIR}/backends/imgui_impl_dx12.cpp"
    "${imgui_source_SOURCE_DIR}/backends/imgui_impl_win32.cpp"
)
target_include_directories(imgui SYSTEM PUBLIC
    "${imgui_source_SOURCE_DIR}"
    "${imgui_source_SOURCE_DIR}/backends"
)
target_compile_definitions(imgui PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(imgui PRIVATE /W0)
target_link_libraries(imgui PUBLIC d3d12 d3dcompiler dxgi)
