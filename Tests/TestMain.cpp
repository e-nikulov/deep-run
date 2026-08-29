#include "Engine/Audio/AudioEngine.h"
#include "Engine/Assets/AssetManager.h"
#include "Engine/Assets/ModelAsset.h"
#include "Engine/Core/CoreServices.h"
#include "Engine/Core/Engine.h"
#include "Engine/Core/EngineConfig.h"
#include "Engine/Core/FixedStepAccumulator.h"
#include "Engine/Core/Random.h"
#include "Engine/Core/Time.h"
#include "Engine/Diagnostics/Logger.h"
#include "Engine/Input/InputState.h"
#include "Engine/Physics/PhysicsWorld.h"
#include "Engine/Render/Camera.h"
#include "Engine/Render/ClearRect.h"
#include "Engine/Render/D3D12Renderer.h"
#include "Engine/Render/IndexedGeometry.h"
#include "Engine/Render/ModelDraw.h"
#include "Engine/Scene/Scene.h"
#include "Game/PhysicsRenderSync.h"
#include "Game/WaterPresentation.h"
#include "Simulation/Marine/WaterBody.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
using Test = std::pair<std::string_view, std::function<bool()>>;
constexpr std::string_view CanonicalModelPath = "submarines/prototype/submarine_prototype.glb";
std::filesystem::path testAssetRoot;

class TemporaryDirectory final
{
public:
    TemporaryDirectory()
    {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() / ("DeepRunTests-" + std::to_string(suffix));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& Path() const noexcept
    {
        return path_;
    }

private:
    std::filesystem::path path_;
};

void WriteFile(const std::filesystem::path& path, const std::string_view contents)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output << contents;
}

void AppendUint32(std::vector<std::byte>& bytes, const std::uint32_t value)
{
    bytes.push_back(static_cast<std::byte>(value & 0xFFU));
    bytes.push_back(static_cast<std::byte>((value >> 8U) & 0xFFU));
    bytes.push_back(static_cast<std::byte>((value >> 16U) & 0xFFU));
    bytes.push_back(static_cast<std::byte>((value >> 24U) & 0xFFU));
}

void AppendFloat(std::vector<std::byte>& bytes, const float value)
{
    AppendUint32(bytes, std::bit_cast<std::uint32_t>(value));
}

void WriteBinaryFile(const std::filesystem::path& path, const std::vector<std::byte>& bytes)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void WriteTriangleGlb(
    const std::filesystem::path& path,
    const std::uint32_t primitiveMode,
    const bool includePosition,
    const bool outOfRangeIndex,
    const bool nonFinitePosition,
    const std::optional<std::size_t> materialIndex = std::nullopt)
{
    std::vector<std::byte> binary;
    const float positionValues[] = {
        nonFinitePosition ? std::numeric_limits<float>::quiet_NaN() : 0.0F, 0.0F, 0.0F,
        1.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F};
    for (const float value : positionValues)
    {
        AppendFloat(binary, value);
    }
    binary.push_back(std::byte{0});
    binary.push_back(std::byte{0});
    binary.push_back(std::byte{1});
    binary.push_back(std::byte{0});
    binary.push_back(static_cast<std::byte>(outOfRangeIndex ? 3 : 2));
    binary.push_back(std::byte{0});

    std::string json =
        R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],)"
        R"("nodes":[{"name":"FixtureNode","mesh":0}],)"
        R"("meshes":[{"name":"FixtureMesh","primitives":[{"attributes":)";
    json += includePosition ? R"({"POSITION":0})" : "{}";
    json += R"(,"indices":1,"mode":)" + std::to_string(primitiveMode);
    if (materialIndex.has_value())
    {
        json += R"(,"material":)" + std::to_string(*materialIndex);
    }
    json += R"(}]}],"materials":[{"name":"FixtureMaterial"}],"buffers":[{"byteLength":42}],)"
            R"("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},)"
            R"({"buffer":0,"byteOffset":36,"byteLength":6}],)"
            R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3",)"
            R"("min":[0,0,0],"max":[1,1,0]},)"
            R"({"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}]})";

    while (json.size() % 4 != 0)
    {
        json.push_back(' ');
    }
    while (binary.size() % 4 != 0)
    {
        binary.push_back(std::byte{0});
    }

    std::vector<std::byte> glb;
    const std::uint32_t totalLength = static_cast<std::uint32_t>(12 + 8 + json.size() + 8 + binary.size());
    AppendUint32(glb, 0x46546C67U);
    AppendUint32(glb, 2U);
    AppendUint32(glb, totalLength);
    AppendUint32(glb, static_cast<std::uint32_t>(json.size()));
    AppendUint32(glb, 0x4E4F534AU);
    for (const char character : json)
    {
        glb.push_back(static_cast<std::byte>(character));
    }
    AppendUint32(glb, static_cast<std::uint32_t>(binary.size()));
    AppendUint32(glb, 0x004E4942U);
    glb.insert(glb.end(), binary.begin(), binary.end());
    WriteBinaryFile(path, glb);
}

bool CanonicalModelLoads()
{
    DeepRun::Assets::AssetManager assets(testAssetRoot);
    const auto loaded = assets.LoadModel(CanonicalModelPath);
    return loaded && loaded->IsValid() && loaded->Get()->id.Value() == CanonicalModelPath;
}

bool CanonicalModelHasIndexedGeometry()
{
    DeepRun::Assets::AssetManager assets(testAssetRoot);
    const auto loaded = assets.LoadModel(CanonicalModelPath);
    if (!loaded || loaded->Get()->primitives.empty())
    {
        return false;
    }

    for (const DeepRun::Assets::MeshPrimitiveData& primitive : loaded->Get()->primitives)
    {
        if (primitive.vertices.empty() || primitive.indices.empty() || primitive.indices.size() % 3 != 0 ||
            !primitive.hasNormals)
        {
            return false;
        }
        for (const std::uint32_t index : primitive.indices)
        {
            if (index >= primitive.vertices.size())
            {
                return false;
            }
        }
    }
    return true;
}

bool CanonicalModelHasFiniteBounds()
{
    DeepRun::Assets::AssetManager assets(testAssetRoot);
    const auto loaded = assets.LoadModel(CanonicalModelPath);
    if (!loaded)
    {
        return false;
    }

    const DeepRun::Assets::ModelBounds& bounds = loaded->Get()->bounds;
    const float values[] = {
        bounds.minimum.x,
        bounds.minimum.y,
        bounds.minimum.z,
        bounds.maximum.x,
        bounds.maximum.y,
        bounds.maximum.z};
    return std::all_of(std::begin(values), std::end(values), [](const float value) { return std::isfinite(value); }) &&
           bounds.maximum.x > bounds.minimum.x && bounds.maximum.y > bounds.minimum.y &&
           bounds.maximum.z > bounds.minimum.z;
}

bool CanonicalModelPreservesMaterial()
{
    DeepRun::Assets::AssetManager assets(testAssetRoot);
    const auto loaded = assets.LoadModel(CanonicalModelPath);
    if (!loaded || loaded->Get()->materials.size() != 1)
    {
        return false;
    }

    const DeepRun::Assets::ModelMaterialData& material = loaded->Get()->materials.front();
    constexpr float Epsilon = 0.0001F;
    const bool factorsMatch = material.name == "M_Submarine_Prototype" &&
                              std::abs(material.baseColorFactor[0] - 0.028F) < Epsilon &&
                              std::abs(material.baseColorFactor[1] - 0.075F) < Epsilon &&
                              std::abs(material.baseColorFactor[2] - 0.105F) < Epsilon &&
                              std::abs(material.baseColorFactor[3] - 1.0F) < Epsilon &&
                              std::abs(material.metallicFactor - 0.15F) < Epsilon &&
                              std::abs(material.roughnessFactor - 0.62F) < Epsilon;
    return factorsMatch &&
           std::all_of(
               loaded->Get()->primitives.begin(),
               loaded->Get()->primitives.end(),
               [](const DeepRun::Assets::MeshPrimitiveData& primitive)
               {
                   return primitive.materialIndex.has_value() && *primitive.materialIndex == 0;
               });
}

bool CanonicalModelPreservesStructure()
{
    DeepRun::Assets::AssetManager assets(testAssetRoot);
    const auto loaded = assets.LoadModel(CanonicalModelPath);
    if (!loaded || loaded->Get()->nodes.size() != 4 || loaded->Get()->primitives.size() != 4)
    {
        return false;
    }

    bool foundHull = false;
    bool foundSail = false;
    bool foundControlSurfaces = false;
    bool foundPropeller = false;
    for (const DeepRun::Assets::MeshNodeData& node : loaded->Get()->nodes)
    {
        if (node.primitiveIndices.size() != 1 || node.primitiveIndices.front() >= loaded->Get()->primitives.size())
        {
            return false;
        }
        if (node.name == "SM_Submarine_Prototype_Hull")
        {
            foundHull = true;
        }
        else if (node.name == "SM_Submarine_Prototype_Sail")
        {
            foundSail = true;
        }
        else if (node.name == "SM_Submarine_Prototype_ControlSurfaces")
        {
            foundControlSurfaces = true;
        }
        else if (node.name == "SM_Submarine_Prototype_Propeller")
        {
            foundPropeller = std::abs(node.localToModel.values[12] + 49.0F) < 0.0001F &&
                             std::abs(node.localToModel.values[13]) < 0.0001F &&
                             std::abs(node.localToModel.values[14]) < 0.0001F;
        }
    }
    return foundHull && foundSail && foundControlSurfaces && foundPropeller;
}

bool CanonicalIndexedGpuLayout()
{
    DeepRun::Assets::AssetManager assets(testAssetRoot);
    const auto loaded = assets.LoadModel(CanonicalModelPath);
    if (!loaded)
    {
        return false;
    }

    const auto layout = DeepRun::Render::BuildIndexedGeometryLayout(**loaded);
    if (!layout || layout->primitives.size() != 4 || layout->totals.primitiveCount != 4 ||
        layout->totals.vertexCount != 296 || layout->totals.indexCount != 1632 ||
        layout->totals.vertexBytes != 7104 || layout->totals.indexBytes != 6528 ||
        layout->totals.uploadCompleted)
    {
        return false;
    }

    constexpr std::array<std::uint32_t, 4> ExpectedVertices{32, 170, 84, 10};
    constexpr std::array<std::uint32_t, 4> ExpectedIndices{144, 1008, 432, 48};
    for (std::size_t index = 0; index < layout->primitives.size(); ++index)
    {
        const DeepRun::Render::IndexedPrimitiveLayout& primitive = layout->primitives[index];
        if (primitive.primitiveIndex != index || primitive.vertexCount != ExpectedVertices[index] ||
            primitive.indexCount != ExpectedIndices[index] ||
            primitive.vertexBytes != ExpectedVertices[index] * sizeof(DeepRun::Assets::MeshVertex) ||
            primitive.indexBytes != ExpectedIndices[index] * sizeof(std::uint32_t))
        {
            return false;
        }
    }
    return true;
}

bool InvalidIndexedGpuLayoutIsRejected()
{
    DeepRun::Assets::AssetManager assets(testAssetRoot);
    const auto loaded = assets.LoadModel(CanonicalModelPath);
    if (!loaded)
    {
        return false;
    }

    DeepRun::Assets::ModelAsset invalid = **loaded;
    invalid.primitives.front().vertices.clear();
    const auto layout = DeepRun::Render::BuildIndexedGeometryLayout(invalid);
    return !layout && layout.error().find("primitive 0") != std::string::npos;
}

bool DefaultGpuModelHandleIsInvalid()
{
    const DeepRun::Render::GpuModelHandle first;
    const DeepRun::Render::GpuModelHandle second;
    return !first.IsValid() && first == second;
}

bool GenericEngineCoreHasNoSubmarineDependency()
{
    const std::filesystem::path engineCore =
        std::filesystem::path(DEEPRUN_SOURCE_ROOT) / "Engine" / "Core";
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::recursive_directory_iterator(engineCore))
    {
        if (!entry.is_regular_file())
        {
            continue;
        }
        const std::filesystem::path extension = entry.path().extension();
        if (extension != ".h" && extension != ".cpp")
        {
            continue;
        }

        std::ifstream input(entry.path(), std::ios::binary);
        std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        std::ranges::transform(contents, contents.begin(), [](const unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        });
        if (contents.find("submarine") != std::string::npos)
        {
            return false;
        }
    }
    return true;
}

bool AutoFitSideViewCameraContract()
{
    DeepRun::Assets::AssetManager assets(testAssetRoot);
    const auto loaded = assets.LoadModel(CanonicalModelPath);
    if (!loaded)
    {
        return false;
    }

    const auto wide = DeepRun::Render::BuildAutoFitSideViewCamera((*loaded)->bounds, 16.0F / 9.0F);
    const auto narrow = DeepRun::Render::BuildAutoFitSideViewCamera((*loaded)->bounds, 4.0F / 3.0F);
    return wide && narrow && wide->viewDirection.x == 0.0F && wide->viewDirection.y == 0.0F &&
           wide->viewDirection.z == -1.0F && wide->up.x == 0.0F && wide->up.y == 1.0F &&
           wide->up.z == 0.0F &&
           wide->position.z > (*loaded)->bounds.maximum.z && wide->nearPlane > 0.0F &&
           wide->farPlane > wide->nearPlane && DeepRun::Render::IsFinite(wide->viewProjection) &&
           DeepRun::Render::IsFinite(narrow->viewProjection) &&
           DeepRun::Render::BoundsFitInCamera((*loaded)->bounds, *wide) &&
           DeepRun::Render::BoundsFitInCamera((*loaded)->bounds, *narrow) &&
           std::abs(wide->width / wide->height - 16.0F / 9.0F) < 0.0001F &&
           std::abs(narrow->width / narrow->height - 4.0F / 3.0F) < 0.0001F;
}

bool FixedWorldCameraSpanContract()
{
    constexpr float HorizontalSpan = 600.0F;
    const DeepRun::Assets::ModelBounds depthBounds{
        .minimum = {-50.0F, -10.0F, -4.0F},
        .maximum = {50.0F, 10.0F, 4.0F}};
    const DeepRun::Assets::ModelVector3 target{};
    const auto sixteenNine = DeepRun::Render::BuildFixedWorldSideViewCamera(
        target, 16.0F / 9.0F, HorizontalSpan, depthBounds);
    const auto sixteenTen = DeepRun::Render::BuildFixedWorldSideViewCamera(
        target, 16.0F / 10.0F, HorizontalSpan, depthBounds);

    return sixteenNine && sixteenTen && std::abs(sixteenNine->width - HorizontalSpan) < 0.0001F &&
           std::abs(sixteenNine->height - 337.5F) < 0.0001F &&
           std::abs(sixteenTen->width - HorizontalSpan) < 0.0001F &&
           std::abs(sixteenTen->height - 375.0F) < 0.0001F &&
           sixteenNine->viewDirection.x == 0.0F && sixteenNine->viewDirection.y == 0.0F &&
           sixteenNine->viewDirection.z == -1.0F && sixteenNine->up.x == 0.0F &&
           sixteenNine->up.y == 1.0F && sixteenNine->up.z == 0.0F &&
           DeepRun::Render::BoundsFitInCamera(depthBounds, *sixteenNine) &&
           DeepRun::Render::IsFinite(sixteenNine->viewProjection);
}

bool FixedWorldCameraIgnoresModelSize()
{
    constexpr float Aspect = 16.0F / 9.0F;
    constexpr float HorizontalSpan = 600.0F;
    const DeepRun::Assets::ModelVector3 smallTarget{5.0F, 7.0F, 0.0F};
    const DeepRun::Assets::ModelBounds smallBounds{
        .minimum = {4.0F, 6.0F, -0.5F},
        .maximum = {6.0F, 8.0F, 0.5F}};
    const DeepRun::Assets::ModelVector3 largeTarget{-200.0F, 400.0F, 30.0F};
    const DeepRun::Assets::ModelBounds largeBounds{
        .minimum = {-700.0F, -100.0F, -70.0F},
        .maximum = {300.0F, 900.0F, 130.0F}};

    const auto smallCamera = DeepRun::Render::BuildFixedWorldSideViewCamera(
        smallTarget, Aspect, HorizontalSpan, smallBounds);
    const auto largeCamera = DeepRun::Render::BuildFixedWorldSideViewCamera(
        largeTarget, Aspect, HorizontalSpan, largeBounds);
    return smallCamera && largeCamera && smallCamera->width == largeCamera->width &&
           smallCamera->height == largeCamera->height && smallCamera->width == HorizontalSpan &&
           smallCamera->target.x == smallTarget.x && largeCamera->target.x == largeTarget.x &&
           largeCamera->nearPlane > 0.0F && largeCamera->farPlane > largeCamera->nearPlane;
}

bool FixedWorldCameraScreenScale()
{
    constexpr float HorizontalSpan = 600.0F;
    const DeepRun::Assets::ModelBounds bounds{
        .minimum = {-50.0F, -10.0F, -2.0F},
        .maximum = {50.0F, 10.0F, 2.0F}};
    const auto camera = DeepRun::Render::BuildFixedWorldSideViewCamera(
        {}, 16.0F / 9.0F, HorizontalSpan, bounds);
    if (!camera)
    {
        return false;
    }

    const std::array<float, 4> left =
        DeepRun::Render::TransformPoint(camera->viewProjection, bounds.minimum);
    const std::array<float, 4> right =
        DeepRun::Render::TransformPoint(camera->viewProjection, bounds.maximum);
    const float viewportFraction = std::abs(right[0] - left[0]) * 0.5F;
    return std::abs(viewportFraction - 100.0F / HorizontalSpan) < 0.0001F;
}

bool FixedWorldCameraTranslationContract()
{
    constexpr float Aspect = 16.0F / 9.0F;
    constexpr float HorizontalSpan = 600.0F;
    const DeepRun::Assets::ModelBounds originBounds{
        .minimum = {-50.0F, -10.0F, -3.0F},
        .maximum = {50.0F, 10.0F, 3.0F}};
    const DeepRun::Assets::ModelVector3 translation{120.0F, -45.0F, 17.0F};
    const DeepRun::Assets::ModelBounds translatedBounds{
        .minimum = {70.0F, -55.0F, 14.0F},
        .maximum = {170.0F, -35.0F, 20.0F}};

    const auto origin = DeepRun::Render::BuildFixedWorldSideViewCamera(
        {}, Aspect, HorizontalSpan, originBounds);
    const auto translated = DeepRun::Render::BuildFixedWorldSideViewCamera(
        translation, Aspect, HorizontalSpan, translatedBounds);
    return origin && translated && translated->target.x == translation.x &&
           translated->target.y == translation.y && translated->target.z == translation.z &&
           std::abs(translated->position.x - origin->position.x - translation.x) < 0.0001F &&
           std::abs(translated->position.y - origin->position.y - translation.y) < 0.0001F &&
           std::abs(translated->position.z - origin->position.z - translation.z) < 0.0001F &&
           translated->width == origin->width && translated->height == origin->height;
}

bool FixedWorldCameraRejectsInvalidInput()
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    const DeepRun::Assets::ModelBounds validBounds{
        .minimum = {-1.0F, -1.0F, -1.0F},
        .maximum = {1.0F, 1.0F, 1.0F}};
    DeepRun::Assets::ModelBounds invalidDepth = validBounds;
    invalidDepth.maximum.z = infinity;
    DeepRun::Assets::ModelBounds invertedDepth = validBounds;
    invertedDepth.minimum.z = 2.0F;

    return !DeepRun::Render::BuildFixedWorldSideViewCamera({}, 0.0F, 600.0F, validBounds) &&
           !DeepRun::Render::BuildFixedWorldSideViewCamera({}, -1.0F, 600.0F, validBounds) &&
           !DeepRun::Render::BuildFixedWorldSideViewCamera({}, 1.0F, 0.0F, validBounds) &&
           !DeepRun::Render::BuildFixedWorldSideViewCamera({}, 1.0F, -600.0F, validBounds) &&
           !DeepRun::Render::BuildFixedWorldSideViewCamera({nan, 0.0F, 0.0F}, 1.0F, 600.0F, validBounds) &&
           !DeepRun::Render::BuildFixedWorldSideViewCamera({}, nan, 600.0F, validBounds) &&
           !DeepRun::Render::BuildFixedWorldSideViewCamera({}, infinity, 600.0F, validBounds) &&
           !DeepRun::Render::BuildFixedWorldSideViewCamera({}, 1.0F, infinity, validBounds) &&
           !DeepRun::Render::BuildFixedWorldSideViewCamera({}, 1.0F, 600.0F, invalidDepth) &&
           !DeepRun::Render::BuildFixedWorldSideViewCamera({}, 1.0F, 600.0F, invertedDepth);
}

bool ModelAndNormalTransformContract()
{
    DeepRun::Assets::AssetManager assets(testAssetRoot);
    const auto loaded = assets.LoadModel(CanonicalModelPath);
    if (!loaded)
    {
        return false;
    }

    const auto draws = DeepRun::Render::PrepareModelDraws(**loaded);
    if (!draws)
    {
        return false;
    }
    const auto propeller = std::ranges::find_if(*draws, [&loaded](const DeepRun::Render::ModelDrawInstance& draw)
    {
        return (*loaded)->nodes[draw.nodeIndex].name == "SM_Submarine_Prototype_Propeller";
    });
    if (propeller == draws->end() || std::abs(propeller->modelToWorld.values[12] + 49.0F) > 0.0001F ||
        std::abs(propeller->modelToWorld.values[13]) > 0.0001F ||
        std::abs(propeller->modelToWorld.values[14]) > 0.0001F)
    {
        return false;
    }

    DeepRun::Assets::ModelTransform translated;
    translated.values[12] = 20.0F;
    translated.values[13] = -7.0F;
    translated.values[14] = 3.0F;
    const auto translatedNormal = DeepRun::Render::BuildNormalTransform(translated);

    DeepRun::Assets::ModelTransform scaled;
    scaled.values[0] = 2.0F;
    scaled.values[5] = 3.0F;
    scaled.values[10] = 4.0F;
    scaled.values[12] = 100.0F;
    const auto scaledNormal = DeepRun::Render::BuildNormalTransform(scaled);
    if (!translatedNormal || !scaledNormal)
    {
        return false;
    }

    const std::array<float, 3> translatedResult =
        DeepRun::Render::TransformNormal(*translatedNormal, {1.0F, 2.0F, 3.0F});
    const std::array<float, 3> scaledResult =
        DeepRun::Render::TransformNormal(*scaledNormal, {2.0F, 3.0F, 4.0F});
    return translatedResult == std::array{1.0F, 2.0F, 3.0F} &&
           std::abs(scaledResult[0] - 1.0F) < 0.0001F &&
           std::abs(scaledResult[1] - 1.0F) < 0.0001F &&
           std::abs(scaledResult[2] - 1.0F) < 0.0001F;
}

bool ModelDrawPreparationContract()
{
    DeepRun::Assets::AssetManager assets(testAssetRoot);
    const auto loaded = assets.LoadModel(CanonicalModelPath);
    if (!loaded)
    {
        return false;
    }

    const auto draws = DeepRun::Render::PrepareModelDraws(**loaded);
    if (!draws || draws->size() != 4)
    {
        return false;
    }
    for (const DeepRun::Render::ModelDrawInstance& draw : *draws)
    {
        if (draw.nodeIndex >= (*loaded)->nodes.size() ||
            draw.primitiveIndex >= (*loaded)->primitives.size() ||
            draw.material.name != "M_Submarine_Prototype" ||
            (*loaded)->nodes[draw.nodeIndex].primitiveIndices.front() != draw.primitiveIndex)
        {
            return false;
        }
    }

    DeepRun::Assets::ModelAsset badPrimitive = **loaded;
    badPrimitive.nodes.front().primitiveIndices.front() = badPrimitive.primitives.size();
    DeepRun::Assets::ModelAsset badMaterial = **loaded;
    badMaterial.primitives.front().materialIndex = badMaterial.materials.size();
    return !DeepRun::Render::PrepareModelDraws(badPrimitive) &&
           !DeepRun::Render::PrepareModelDraws(badMaterial);
}

bool InvalidGpuModelDrawIsRejected()
{
    DeepRun::Diagnostics::Logger logger;
    DeepRun::Render::D3D12Renderer renderer(logger);
    const DeepRun::Render::GpuModelHandle invalid;
    const auto result = renderer.DrawModel(invalid, {}, {});
    return !result && result.error().find("invalid or foreign") != std::string::npos;
}

bool ModelIdentityAndCache()
{
    DeepRun::Assets::AssetManager assets(testAssetRoot);
    const auto first = assets.LoadModel(CanonicalModelPath);
    const auto duplicate = assets.LoadModel("submarines/prototype/./submarine_prototype.glb");
    if (!first || !duplicate || first->Get() != duplicate->Get() || *first != *duplicate ||
        assets.CachedResourceCount() != 1)
    {
        return false;
    }

    const DeepRun::Assets::AssetHandle<DeepRun::Assets::ModelAsset> handle = *first;
    assets.Clear();
    return assets.CachedResourceCount() == 0 && !handle.IsValid() && handle.Get() == nullptr;
}

bool MissingModelAsset()
{
    DeepRun::Assets::AssetManager assets(testAssetRoot);
    const auto missing = assets.LoadModel("submarines/prototype/missing.glb");
    return !missing && missing.error().code == DeepRun::Assets::AssetErrorCode::NotFound &&
           missing.error().message.find("submarines/prototype/missing.glb") != std::string::npos;
}

bool InvalidModelDataIsRejected()
{
    TemporaryDirectory temporary;
    WriteTriangleGlb(temporary.Path() / "missing-position.glb", 4, false, false, false);
    WriteTriangleGlb(temporary.Path() / "lines.glb", 1, true, false, false);
    WriteTriangleGlb(temporary.Path() / "bad-index.glb", 4, true, true, false);
    WriteTriangleGlb(temporary.Path() / "non-finite.glb", 4, true, false, true);

    DeepRun::Assets::AssetManager assets(temporary.Path());
    const auto missingPosition = assets.LoadModel("missing-position.glb");
    const auto lines = assets.LoadModel("lines.glb");
    const auto badIndex = assets.LoadModel("bad-index.glb");
    const auto nonFinite = assets.LoadModel("non-finite.glb");
    return !missingPosition && !lines && !badIndex && !nonFinite &&
           missingPosition.error().code == DeepRun::Assets::AssetErrorCode::InvalidData &&
           missingPosition.error().message.find("POSITION") != std::string::npos &&
           lines.error().code == DeepRun::Assets::AssetErrorCode::UnsupportedData &&
           lines.error().message.find("TRIANGLES") != std::string::npos &&
           badIndex.error().code == DeepRun::Assets::AssetErrorCode::InvalidData &&
           badIndex.error().message.find("out of range") != std::string::npos &&
           nonFinite.error().code == DeepRun::Assets::AssetErrorCode::InvalidData &&
           nonFinite.error().message.find("non-finite") != std::string::npos &&
           assets.CachedResourceCount() == 0;
}

bool MaterialDefaultsAndInvalidReference()
{
    TemporaryDirectory temporary;
    WriteTriangleGlb(temporary.Path() / "default-material.glb", 4, true, false, false, 0);
    WriteTriangleGlb(temporary.Path() / "bad-material.glb", 4, true, false, false, 1);

    DeepRun::Assets::AssetManager assets(temporary.Path());
    const auto defaults = assets.LoadModel("default-material.glb");
    if (!defaults || defaults->Get()->materials.size() != 1 || defaults->Get()->primitives.size() != 1)
    {
        return false;
    }

    const DeepRun::Assets::ModelMaterialData& material = defaults->Get()->materials.front();
    const DeepRun::Assets::MeshPrimitiveData& primitive = defaults->Get()->primitives.front();
    const auto malformed = assets.LoadModel("bad-material.glb");
    return material.name == "FixtureMaterial" &&
           material.baseColorFactor == std::array{1.0F, 1.0F, 1.0F, 1.0F} &&
           material.metallicFactor == 1.0F && material.roughnessFactor == 1.0F &&
           primitive.materialIndex.has_value() && *primitive.materialIndex == 0 &&
           !malformed && malformed.error().code == DeepRun::Assets::AssetErrorCode::InvalidData &&
           malformed.error().message.find("material index") != std::string::npos;
}

bool CoreStartupShutdown()
{
    DeepRun::Core::CoreServices core;
    core.Log().Info(DeepRun::Diagnostics::LogCategory::Core, "Core lifecycle test active");
    return true;
}

bool FrameLifecycle()
{
    DeepRun::Core::FrameTimer timer;
    timer.Reset();
    timer.Advance(0.01);
    timer.Advance(0.50);
    return timer.FrameIndex() == 2 && std::abs(timer.DeltaSeconds() - 0.25) < 0.0001 &&
           std::abs(timer.ElapsedSeconds() - 0.26) < 0.0001;
}

bool FrameRebasePreservesState()
{
    DeepRun::Core::FrameTimer timer;
    timer.Reset();
    timer.Advance(0.01);
    timer.Advance(0.02);

    const std::uint64_t frameIndex = timer.FrameIndex();
    const double elapsedSeconds = timer.ElapsedSeconds();
    timer.Rebase();

    return timer.FrameIndex() == frameIndex &&
           std::abs(timer.ElapsedSeconds() - elapsedSeconds) < 0.0001 &&
           timer.DeltaSeconds() == 0.0;
}

bool FixedStepScheduling()
{
    constexpr double FixedStep = 1.0 / 60.0;
    DeepRun::Core::FixedStepAccumulator accumulator(FixedStep);
    if (accumulator.Accumulate(1.0 / 120.0) != 0 || accumulator.Accumulate(1.0 / 120.0) != 1)
    {
        return false;
    }

    accumulator.Reset();
    return accumulator.Accumulate(1.0 / 30.0) == 2 &&
           std::abs(accumulator.RemainingSeconds()) < FixedStep * 1.0e-8;
}

bool EngineHeadlessLifecycle()
{
    TemporaryDirectory temporary;
    const std::filesystem::path configPath = temporary.Path() / "engine.json";
    WriteFile(
        configPath,
        R"({"renderer":{"vsync":true,"width":800,"height":600},"physics":{"fixedHz":60}})");
    WriteFile(temporary.Path() / "Content" / "lifetime.txt", "manager owned");

    DeepRun::Core::Engine engine({
        .headless = true,
        .configPath = configPath,
        .contentRoot = temporary.Path() / "Content"});
    if (!engine.Initialize() || engine.Lifecycle() != DeepRun::Core::EngineLifecycle::Running)
    {
        return false;
    }
    const DeepRun::Scene::Entity entity = engine.ActiveScene().CreateEntity("shutdown-order");
    const auto assetResult = engine.Assets().LoadText("lifetime.txt");
    if (!engine.ActiveScene().IsValid(entity) || !assetResult || !assetResult->IsValid())
    {
        return false;
    }
    const DeepRun::Assets::AssetHandle<DeepRun::Assets::TextAsset> asset = *assetResult;
    if (engine.Update() || engine.Lifecycle() != DeepRun::Core::EngineLifecycle::ShutdownRequested ||
        engine.CurrentFrame().frameIndex != 1 || engine.ExitCode() != 0)
    {
        return false;
    }
    engine.Shutdown();
    return engine.Lifecycle() == DeepRun::Core::EngineLifecycle::Stopped &&
           engine.ActiveScene().EntityCount() == 0 && engine.Assets().CachedResourceCount() == 0 &&
           !asset.IsValid();
}

bool SceneEntityLifecycle()
{
    struct Counter final
    {
        int value = 0;
    };

    DeepRun::Scene::Scene scene;
    const DeepRun::Scene::Entity first = scene.CreateEntity("first");
    const DeepRun::Scene::Entity second = scene.CreateEntity();
    if (!scene.IsValid(first) || !scene.IsValid(second) || scene.EntityCount() != 2 ||
        !scene.Has<DeepRun::Scene::Transform>(first) || !scene.Has<DeepRun::Scene::Tag>(first))
    {
        return false;
    }

    scene.Get<DeepRun::Scene::Transform>(first).position = {3.0F, 4.0F, 5.0F};
    scene.Add<Counter>(first, 7);
    scene.Add<Counter>(second, 11);
    int total = 0;
    scene.Each<Counter>([&total](DeepRun::Scene::Entity, Counter& counter) { total += counter.value; });
    if (total != 18 || scene.Get<DeepRun::Scene::Transform>(first).position != DeepRun::Scene::Float3{3.0F, 4.0F, 5.0F})
    {
        return false;
    }

    scene.DestroyEntity(first);
    if (scene.IsValid(first) || scene.EntityCount() != 1)
    {
        return false;
    }
    const DeepRun::Scene::Entity replacement = scene.CreateEntity("replacement");
    if (!scene.IsValid(replacement) || scene.IsValid(first) || replacement == first)
    {
        return false;
    }
    scene.DestroyEntity(first);
    scene.Clear();
    return !scene.IsValid(second) && !scene.IsValid(replacement) && scene.EntityCount() == 0;
}

bool ResourceIdentityAndCache()
{
    TemporaryDirectory temporary;
    WriteFile(temporary.Path() / "Definitions" / "example.json", "resource payload");

    DeepRun::Assets::AssetManager assets(temporary.Path());
    const auto first = assets.LoadText("Definitions/example.json");
    const auto duplicate = assets.LoadText("Definitions/./example.json");
    const auto missing = assets.LoadText("missing.json");
    const auto traversal = assets.LoadText("../outside.json");
    if (!first || !duplicate || !first->IsValid() || !duplicate->IsValid() || *first != *duplicate ||
        first->Get() != duplicate->Get() || first->Get()->text != "resource payload" ||
        assets.CachedResourceCount() != 1 || missing || traversal ||
        missing.error().code != DeepRun::Assets::AssetErrorCode::NotFound ||
        traversal.error().code != DeepRun::Assets::AssetErrorCode::InvalidPath)
    {
        return false;
    }

    assets.Clear();
    if (assets.CachedResourceCount() != 0 || first->IsValid() || first->Get() != nullptr)
    {
        return false;
    }

    DeepRun::Assets::AssetHandle<DeepRun::Assets::TextAsset> managerLifetime;
    {
        DeepRun::Assets::AssetManager scopedAssets(temporary.Path());
        const auto loaded = scopedAssets.LoadText("Definitions/example.json");
        if (!loaded || !loaded->IsValid())
        {
            return false;
        }
        managerLifetime = *loaded;
    }
    return !managerLifetime.IsValid() && managerLifetime.Get() == nullptr;
}

bool AssetPathNormalization()
{
    const auto normalized = DeepRun::Assets::AssetId::FromPath("Definitions/./MixedCase.JSON");
    const auto differentCase = DeepRun::Assets::AssetId::FromPath("Definitions/mixedcase.json");
    const auto escaped = DeepRun::Assets::AssetId::FromPath("Definitions/../../outside.json");
    return normalized && normalized->Value() == "Definitions/MixedCase.JSON" && differentCase &&
           normalized->Value() != differentCase->Value() && !escaped;
}

bool ConfigurationLoading()
{
    TemporaryDirectory temporary;
    const std::filesystem::path validPath = temporary.Path() / "valid.json";
    const std::filesystem::path malformedPath = temporary.Path() / "malformed.json";
    const std::filesystem::path invalidPath = temporary.Path() / "invalid.json";
    const std::filesystem::path wrongTypePath = temporary.Path() / "wrong-type.json";
    WriteFile(
        validPath,
        R"({"renderer":{"vsync":false,"width":1920,"height":1080},"physics":{"fixedHz":120}})");
    WriteFile(malformedPath, R"({"renderer":)");
    WriteFile(
        invalidPath,
        R"({"renderer":{"vsync":true,"width":0,"height":720},"physics":{"fixedHz":60}})");
    WriteFile(
        wrongTypePath,
        R"({"renderer":{"vsync":"yes","width":1920,"height":1080},"physics":{"fixedHz":60}})");

    const auto valid = DeepRun::Core::LoadEngineConfig(validPath);
    const auto malformed = DeepRun::Core::LoadEngineConfig(malformedPath);
    const auto invalid = DeepRun::Core::LoadEngineConfig(invalidPath);
    const auto wrongType = DeepRun::Core::LoadEngineConfig(wrongTypePath);
    const auto missing = DeepRun::Core::LoadEngineConfig(temporary.Path() / "missing.json");
    return valid && valid->renderer.width == 1920 && valid->renderer.height == 1080 &&
           !valid->renderer.vsync && valid->physics.fixedHz == 120 && !malformed &&
           malformed.error().code == DeepRun::Core::ConfigErrorCode::InvalidJson && !invalid && !wrongType &&
           wrongType.error().code == DeepRun::Core::ConfigErrorCode::InvalidValue &&
           wrongType.error().message.find("renderer.vsync") != std::string::npos && !missing &&
           missing.error().code == DeepRun::Core::ConfigErrorCode::FileNotFound;
}

bool InputStateTransitions()
{
    DeepRun::Input::InputState input;
    if (input.Gamepad().connected)
    {
        return false;
    }

    input.SetActionDown(DeepRun::Input::InputAction::Quit, true);
    input.SetMousePosition(12, 34);
    input.SetMouseButtonDown(0, true);
    if (!input.IsDown(DeepRun::Input::InputAction::Quit) ||
        !input.WasPressed(DeepRun::Input::InputAction::Quit) || input.MouseX() != 12 || input.MouseY() != 34 ||
        !input.IsMouseButtonDown(0))
    {
        return false;
    }

    input.BeginFrame();
    if (!input.IsDown(DeepRun::Input::InputAction::Quit) || input.WasPressed(DeepRun::Input::InputAction::Quit))
    {
        return false;
    }
    input.SetActionDown(DeepRun::Input::InputAction::Quit, false);
    return !input.IsDown(DeepRun::Input::InputAction::Quit) &&
           input.WasReleased(DeepRun::Input::InputAction::Quit);
}

bool DeterministicRandom()
{
    DeepRun::Core::Random first(0xD33F1234U);
    DeepRun::Core::Random second(0xD33F1234U);
    for (int index = 0; index < 32; ++index)
    {
        if (first.NextUInt() != second.NextUInt())
        {
            return false;
        }
    }
    return true;
}

bool JoltInitialization()
{
    DeepRun::Diagnostics::Logger logger;
    DeepRun::Physics::PhysicsWorld physics(logger);
    return physics.Initialize() && physics.IsInitialized();
}

bool RigidBodySimulation()
{
    DeepRun::Diagnostics::Logger logger;
    DeepRun::Physics::PhysicsWorld physics(logger);
    return physics.Initialize() && physics.RunGravitySmokeTest();
}

DeepRun::Physics::DynamicBoxBodyCreateInfo ValidBoxBodyInfo()
{
    DeepRun::Physics::DynamicBoxBodyCreateInfo info;
    info.halfExtents = {0.5F, 0.25F, 1.0F};
    info.mass = 4.0F;
    info.position = {1.5F, -2.25F, 0.75F};
    // 90 degrees around +Y in DeepRun x,y,z,w order: (0, sin(45 deg), 0, cos(45 deg)).
    info.orientation = {0.0F, std::sqrt(0.5F), 0.0F, std::sqrt(0.5F)};
    return info;
}

bool PhysicsHandleSemantics()
{
    const DeepRun::Physics::PhysicsBodyHandle firstDefault;
    const DeepRun::Physics::PhysicsBodyHandle secondDefault;
    if (firstDefault.IsValid() || firstDefault != secondDefault)
    {
        return false;
    }

    // A world that never initializes rejects creation as a recoverable error.
    {
        DeepRun::Diagnostics::Logger logger;
        DeepRun::Physics::PhysicsWorld uninitialized(logger);
        DeepRun::Physics::PhysicsError error;
        const auto rejected = uninitialized.CreateDynamicBoxBody(ValidBoxBodyInfo(), &error);
        if (rejected.IsValid() || error.code != DeepRun::Physics::PhysicsErrorCode::NotInitialized)
        {
            return false;
        }
    }

    // World A creates a body: the handle is valid in A and foreign everywhere else.
    DeepRun::Physics::PhysicsBodyHandle foreignHandle;
    {
        DeepRun::Diagnostics::Logger loggerA;
        DeepRun::Physics::PhysicsWorld worldA(loggerA);
        if (!worldA.Initialize())
        {
            return false;
        }
        const auto handle = worldA.CreateDynamicBoxBody(ValidBoxBodyInfo());
        if (!handle.IsValid() || !worldA.GetBodyState(handle))
        {
            return false;
        }
        foreignHandle = handle;
    }

    DeepRun::Diagnostics::Logger loggerB;
    DeepRun::Physics::PhysicsWorld worldB(loggerB);
    if (!worldB.Initialize())
    {
        return false;
    }
    if (worldB.GetBodyState(foreignHandle))
    {
        return false; // foreign-world handle must be rejected
    }

    DeepRun::Physics::PhysicsError error;
    if (worldB.DestroyBody(foreignHandle, &error) || error.code != DeepRun::Physics::PhysicsErrorCode::InvalidHandle)
    {
        return false;
    }

    // Destruction: the old handle is invalid immediately and stays invalid.
    const auto first = worldB.CreateDynamicBoxBody(ValidBoxBodyInfo());
    if (!first.IsValid() || !worldB.DestroyBody(first))
    {
        return false;
    }
    if (worldB.GetBodyState(first) || worldB.DestroyBody(first, &error) ||
        error.code != DeepRun::Physics::PhysicsErrorCode::InvalidHandle)
    {
        return false;
    }

    // A new body in the same world must not resurrect the stale handle.
    const auto second = worldB.CreateDynamicBoxBody(ValidBoxBodyInfo());
    return second.IsValid() && first != second && !worldB.GetBodyState(first) &&
           worldB.GetBodyState(second).has_value();
}

bool DynamicBoxInputValidation()
{
    DeepRun::Diagnostics::Logger logger;
    DeepRun::Physics::PhysicsWorld world(logger);
    if (!world.Initialize())
    {
        return false;
    }

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    auto valid = ValidBoxBodyInfo();

    auto zeroExtent = valid;
    zeroExtent.halfExtents.x = 0.0F;
    auto negativeExtent = valid;
    negativeExtent.halfExtents.y = -1.0F;
    auto infiniteExtent = valid;
    infiniteExtent.halfExtents.z = infinity;
    auto nanExtent = valid;
    nanExtent.halfExtents.x = nan;
    auto zeroMass = valid;
    zeroMass.mass = 0.0F;
    auto negativeMass = valid;
    negativeMass.mass = -2.0F;
    auto nanMass = valid;
    nanMass.mass = nan;
    auto infiniteMass = valid;
    infiniteMass.mass = infinity;
    auto nonFinitePosition = valid;
    nonFinitePosition.position.y = nan;
    auto zeroQuaternion = valid;
    zeroQuaternion.orientation = {0.0F, 0.0F, 0.0F, 0.0F};
    auto nanQuaternion = valid;
    nanQuaternion.orientation.w = nan;
    auto negativeLinearDamping = valid;
    negativeLinearDamping.linearDamping = -0.1F;
    auto negativeAngularDamping = valid;
    negativeAngularDamping.angularDamping = -0.1F;
    auto nanDamping = valid;
    nanDamping.linearDamping = nan;

    for (const auto& info : {zeroExtent, negativeExtent, infiniteExtent, nanExtent, zeroMass, negativeMass,
                             nanMass, infiniteMass, nonFinitePosition, zeroQuaternion, nanQuaternion,
                             negativeLinearDamping, negativeAngularDamping, nanDamping})
    {
        DeepRun::Physics::PhysicsError error;
        const auto handle = world.CreateDynamicBoxBody(info, &error);
        if (handle.IsValid() || error.code != DeepRun::Physics::PhysicsErrorCode::InvalidInput ||
            error.message.empty())
        {
            return false;
        }
    }

    // Control: the valid input is still accepted after all rejections.
    const auto handle = world.CreateDynamicBoxBody(valid);
    return handle.IsValid() && world.GetBodyState(handle).has_value();
}

bool PhysicsPoseRoundTrip()
{
    DeepRun::Diagnostics::Logger logger;
    DeepRun::Physics::PhysicsWorld world(logger);
    if (!world.Initialize())
    {
        return false;
    }

    const auto info = ValidBoxBodyInfo();
    const auto handle = world.CreateDynamicBoxBody(info);
    if (!handle.IsValid())
    {
        return false;
    }
    const auto state = world.GetBodyState(handle);
    if (!state || !state->active)
    {
        return false;
    }

    constexpr float PositionTolerance = 0.0001F;
    const bool positionMatches = std::abs(state->position.x - info.position.x) < PositionTolerance &&
                                 std::abs(state->position.y - info.position.y) < PositionTolerance &&
                                 std::abs(state->position.z - info.position.z) < PositionTolerance;

    // q and -q are the same rotation, so compare through |dot|, never component equality.
    if (!DeepRun::Physics::PhysicsQuaternion::SameRotation(state->orientation, info.orientation))
    {
        return false;
    }

    // A non-unit but valid input quaternion must normalize to the same rotation.
    auto scaled = info;
    scaled.orientation.x *= 1.5F;
    scaled.orientation.y *= 1.5F;
    scaled.orientation.z *= 1.5F;
    scaled.orientation.w *= 1.5F;
    const auto scaledHandle = world.CreateDynamicBoxBody(scaled);
    const auto scaledState = world.GetBodyState(scaledHandle);
    return positionMatches && scaledState.has_value() &&
           DeepRun::Physics::PhysicsQuaternion::SameRotation(scaledState->orientation, info.orientation);
}

bool PhysicsGravityFalls()
{
    DeepRun::Diagnostics::Logger logger;
    DeepRun::Physics::PhysicsWorld world(logger);
    if (!world.Initialize())
    {
        return false;
    }

    auto info = ValidBoxBodyInfo();
    info.position = {0.0F, 5.0F, 0.0F};
    const auto handle = world.CreateDynamicBoxBody(info);
    const auto initial = world.GetBodyState(handle);
    if (!handle.IsValid() || !initial)
    {
        return false;
    }

    constexpr int Steps = 60; // exactly one second at the existing 60 Hz fixed step
    for (int step = 0; step < Steps; ++step)
    {
        world.Step(1.0F / 60.0F);
    }
    const auto finalState = world.GetBodyState(handle);
    if (!finalState)
    {
        return false;
    }

    const bool finite = std::isfinite(finalState->position.x) && std::isfinite(finalState->position.y) &&
                        std::isfinite(finalState->position.z) && std::isfinite(finalState->linearVelocity.x) &&
                        std::isfinite(finalState->linearVelocity.y) && std::isfinite(finalState->linearVelocity.z) &&
                        std::isfinite(finalState->angularVelocity.x) && std::isfinite(finalState->angularVelocity.y) &&
                        std::isfinite(finalState->angularVelocity.z);
    return finalState->position.y < initial->position.y && finalState->linearVelocity.y < 0.0F &&
           std::abs(finalState->position.x) < 0.001F && std::abs(finalState->position.z) < 0.001F && finite;
}

bool PhysicsGravityDisabledStaysStill()
{
    DeepRun::Diagnostics::Logger logger;
    DeepRun::Physics::PhysicsWorld world(logger);
    if (!world.Initialize())
    {
        return false;
    }

    auto info = ValidBoxBodyInfo();
    info.position = {0.0F, 5.0F, 0.0F};
    info.gravityEnabled = false;
    const auto handle = world.CreateDynamicBoxBody(info);
    const auto initial = world.GetBodyState(handle);
    if (!handle.IsValid() || !initial)
    {
        return false;
    }

    constexpr int Steps = 60;
    for (int step = 0; step < Steps; ++step)
    {
        world.Step(1.0F / 60.0F);
    }
    const auto finalState = world.GetBodyState(handle);
    if (!finalState)
    {
        return false;
    }

    constexpr float Tolerance = 0.0001F;
    return std::abs(finalState->position.x - initial->position.x) < Tolerance &&
           std::abs(finalState->position.y - initial->position.y) < Tolerance &&
           std::abs(finalState->position.z - initial->position.z) < Tolerance &&
           std::abs(finalState->linearVelocity.x) < Tolerance && std::abs(finalState->linearVelocity.y) < Tolerance &&
           std::abs(finalState->linearVelocity.z) < Tolerance;
}

bool NoHiddenLinearDrag()
{
    DeepRun::Diagnostics::Logger logger;
    DeepRun::Physics::PhysicsWorld world(logger);
    if (!world.Initialize())
    {
        return false;
    }

    auto info = ValidBoxBodyInfo();
    info.position = {0.0F, 5.0F, 0.0F};
    info.gravityEnabled = false;
    info.linearDamping = 0.0F; // explicit: no hidden water-like resistance in the generic API
    info.angularDamping = 0.0F;
    info.initialLinearVelocity = {10.0F, 0.0F, 0.0F};
    const auto handle = world.CreateDynamicBoxBody(info);
    const auto initial = world.GetBodyState(handle);
    if (!handle.IsValid() || !initial)
    {
        return false;
    }

    constexpr int Steps = 60; // one second of simulation
    for (int step = 0; step < Steps; ++step)
    {
        world.Step(1.0F / 60.0F);
    }
    const auto finalState = world.GetBodyState(handle);
    if (!finalState)
    {
        return false;
    }

    // Guardrail for the future HydroDragSystem: with zero damping the speed must not decay on its own.
    return std::abs(finalState->linearVelocity.x - 10.0F) < 0.05F &&
           std::abs(finalState->linearVelocity.y) < 0.001F && std::abs(finalState->linearVelocity.z) < 0.001F &&
           std::abs(finalState->position.x - (initial->position.x + 10.0F)) < 0.1F;
}

bool AngularVelocityState()
{
    DeepRun::Diagnostics::Logger logger;
    DeepRun::Physics::PhysicsWorld world(logger);
    if (!world.Initialize())
    {
        return false;
    }

    auto info = ValidBoxBodyInfo();
    info.position = {0.0F, 5.0F, 0.0F};
    info.gravityEnabled = false;
    info.initialAngularVelocity = {0.0F, 2.0F, 0.0F}; // rad/s around +Y
    const auto handle = world.CreateDynamicBoxBody(info);
    const auto initial = world.GetBodyState(handle);
    if (!handle.IsValid() || !initial)
    {
        return false;
    }

    constexpr int Steps = 30; // half a second: about one radian of rotation
    for (int step = 0; step < Steps; ++step)
    {
        world.Step(1.0F / 60.0F);
    }
    const auto finalState = world.GetBodyState(handle);
    if (!finalState)
    {
        return false;
    }

    const bool finite = std::isfinite(finalState->orientation.x) && std::isfinite(finalState->orientation.y) &&
                        std::isfinite(finalState->orientation.z) && std::isfinite(finalState->orientation.w) &&
                        std::isfinite(finalState->angularVelocity.x) && std::isfinite(finalState->angularVelocity.y) &&
                        std::isfinite(finalState->angularVelocity.z);
    return !DeepRun::Physics::PhysicsQuaternion::SameRotation(initial->orientation, finalState->orientation) &&
           std::abs(finalState->angularVelocity.y - 2.0F) < 0.1F && finite;
}

bool AudioBoundary()
{
    DeepRun::Diagnostics::Logger logger;
    DeepRun::Audio::AudioEngine audio(logger);
    const bool initialized = audio.Initialize();
    if (!initialized)
    {
        std::cout << "[SKIP] Audio device unavailable: " << audio.Status() << '\n';
    }
    return !audio.Status().empty();
}
} // namespace

// ---------------------------------------------------------------------------
// M2 Slice C2: physics-to-render synchronization math (pure helpers, no Jolt/D3D12)
// ---------------------------------------------------------------------------

namespace
{
using DeepRun::Assets::ModelBounds;
using DeepRun::Assets::ModelTransform;
using DeepRun::Assets::ModelVector3;
using DeepRun::Physics::PhysicsBodyState;
using DeepRun::Physics::PhysicsQuaternion;
using DeepRun::Render::Multiply;

// Synthetic bounds deliberately NOT centered at the origin, so a pivot bug cannot hide behind symmetry.
const ModelBounds OffCenterTestBounds{
    .minimum = {-30.0F, -5.0F, -4.0F},
    .maximum = {70.0F, 9.0F, 6.0F}};

bool MatrixIsIdentityWithinTolerance(const ModelTransform& matrix, const float tolerance)
{
    for (std::size_t index = 0; index < matrix.values.size(); ++index)
    {
        const float expected = (index % 5 == 0) ? 1.0F : 0.0F; // diagonal entries are 1, the rest 0
        if (std::abs(matrix.values[index] - expected) > tolerance)
        {
            return false;
        }
    }
    return true;
}

ModelVector3 TransformPointBy(const ModelTransform& matrix, const ModelVector3& point)
{
    auto element = [](const ModelTransform& m, const std::size_t row, const std::size_t column) noexcept {
        return m.values[column * 4 + row];
    };
    return {
        element(matrix, 0, 0) * point.x + element(matrix, 0, 1) * point.y + element(matrix, 0, 2) * point.z +
            element(matrix, 0, 3),
        element(matrix, 1, 0) * point.x + element(matrix, 1, 1) * point.y + element(matrix, 1, 2) * point.z +
            element(matrix, 1, 3),
        element(matrix, 2, 0) * point.x + element(matrix, 2, 1) * point.y + element(matrix, 2, 2) * point.z +
            element(matrix, 2, 3)};
}

PhysicsBodyState IdentityStateAt(const ModelVector3& position)
{
    PhysicsBodyState state{};
    state.position = {position.x, position.y, position.z};
    state.orientation = {0.0F, 0.0F, 0.0F, 1.0F}; // x, y, z, w identity
    state.active = true;
    return state;
}

// The exact composition PhysicalPlayground uses (M2 Slice C2.1): bodyToWorld * modelToBody(T(-boundsCenter)).
// BuildBodyToWorld depends only on the physics pose; the pivot correction is applied explicitly here, exactly
// as in Game/PhysicalPlayground.cpp Render().
ModelTransform ComposeRenderTransform(const PhysicsBodyState& state, const ModelVector3& boundsCenter)
{
    const auto bodyToWorld = DeepRun::Game::BuildBodyToWorld(state);
    return Multiply(*bodyToWorld, DeepRun::Game::TranslationTransform({-boundsCenter.x, -boundsCenter.y, -boundsCenter.z}));
}

// 90 degrees around +Z in DeepRun x,y,z,w order: (0, 0, sin(45 deg), cos(45 deg)).
const PhysicsQuaternion QuarterTurnAroundZ{0.0F, 0.0F, std::sqrt(0.5F), std::sqrt(0.5F)};

bool PhysicsRenderSyncPivotContract()
{
    const ModelBounds& bounds = OffCenterTestBounds;
    std::string message;
    if (!DeepRun::Game::ValidateCollisionBounds(bounds, message))
    {
        return false;
    }

    const ModelVector3 center = DeepRun::Game::BoundsCenter(bounds);
    // The synthetic bounds are not symmetric: the center must be computed, never assumed to be (0, 0, 0).
    if (std::abs(center.x - 20.0F) > 1.0e-4F || std::abs(center.y - 2.0F) > 1.0e-4F ||
        std::abs(center.z - 1.0F) > 1.0e-4F)
    {
        return false;
    }

    // Initial body pose: position = boundsCenter, orientation = identity (exactly what Initialize creates).
    const PhysicsBodyState initial = IdentityStateAt(center);
    const ModelTransform modelToWorld = ComposeRenderTransform(initial, center);
    if (!MatrixIsIdentityWithinTolerance(modelToWorld, 1.0e-4F))
    {
        return false; // the first visual frame must be identical to B2.1: T(c) * T(-c) = identity
    }

    // The same contract must hold for a symmetric bounds too (regression guard in both directions).
    const ModelBounds symmetric{
        .minimum = {-50.0F, -8.0F, -10.5F},
        .maximum = {52.0F, 11.4F, 10.5F}}; // canonical-like but still off-center on X/Y
    const ModelVector3 symmetricCenter = DeepRun::Game::BoundsCenter(symmetric);
    return MatrixIsIdentityWithinTolerance(ComposeRenderTransform(IdentityStateAt(symmetricCenter), symmetricCenter),
                                           1.0e-4F);
}

bool PhysicsRenderSyncTranslationAppliedOnce()
{
    const ModelVector3 center = DeepRun::Game::BoundsCenter(OffCenterTestBounds);
    // Known body displacement: +10 X, -20 Y (Z untouched).
    PhysicsBodyState state = IdentityStateAt(center);
    state.position.x += 10.0F;
    state.position.y -= 20.0F;

    const ModelTransform modelToWorld = ComposeRenderTransform(state, center);
    // The model must move exactly +10 X / -20 Y relative to identity: the body translation is applied once,
    // and the pivot correction cancels it at rest instead of doubling or dropping it.
    constexpr float Tolerance = 1.0e-3F;
    return std::abs(modelToWorld.values[12] - 10.0F) < Tolerance &&
           std::abs(modelToWorld.values[13] + 20.0F) < Tolerance && std::abs(modelToWorld.values[14]) < Tolerance &&
           MatrixIsIdentityWithinTolerance(
               ModelTransform{.values = {
                   modelToWorld.values[0], modelToWorld.values[1], modelToWorld.values[2], 0.0F,
                   modelToWorld.values[4], modelToWorld.values[5], modelToWorld.values[6], 0.0F,
                   modelToWorld.values[8], modelToWorld.values[9], modelToWorld.values[10], 0.0F,
                   0.0F, 0.0F, 0.0F, 1.0F}},
               1.0e-4F);
}

bool PhysicsRenderSyncZRotationConvention()
{
    const ModelVector3 center = DeepRun::Game::BoundsCenter(OffCenterTestBounds);
    PhysicsBodyState state = IdentityStateAt(center);
    state.orientation = QuarterTurnAroundZ; // +90 degrees around Z, right-handed: +X must map to +Y

    const ModelTransform modelToWorld = ComposeRenderTransform(state, center);
    const ModelVector3 originImage = TransformPointBy(modelToWorld, {0.0F, 0.0F, 0.0F});
    const ModelVector3 bowImage = TransformPointBy(modelToWorld, {50.0F, 0.0F, 0.0F});

    // The bounds center (model space) must land exactly on the body position: that is the pivot contract.
    constexpr float Tolerance = 1.0e-3F;
    const bool centerLandsOnBody = std::abs(TransformPointBy(modelToWorld, center).x - center.x) < Tolerance &&
                                   std::abs(TransformPointBy(modelToWorld, center).y - center.y) < Tolerance &&
                                   std::abs(TransformPointBy(modelToWorld, center).z - center.z) < Tolerance;

    // A 50 m offset along +X must become a 50 m offset along +Y (rigid rotation, no scale, no mirror).
    const ModelVector3 delta{bowImage.x - originImage.x, bowImage.y - originImage.y, bowImage.z - originImage.z};
    return centerLandsOnBody && std::abs(delta.x) < Tolerance && std::abs(delta.y - 50.0F) < Tolerance &&
           std::abs(delta.z) < Tolerance;
}

bool PhysicsRenderSyncQuaternionSignEquivalence()
{
    const ModelVector3 center = DeepRun::Game::BoundsCenter(OffCenterTestBounds);
    PhysicsBodyState state = IdentityStateAt(center);
    state.orientation = QuarterTurnAroundZ;
    const ModelTransform positive = ComposeRenderTransform(state, center);

    // q and -q are the same rotation: the transform must be equivalent element-wise.
    state.orientation = {-QuarterTurnAroundZ.x, -QuarterTurnAroundZ.y, -QuarterTurnAroundZ.z, -QuarterTurnAroundZ.w};
    const ModelTransform negative = ComposeRenderTransform(state, center);

    for (std::size_t index = 0; index < positive.values.size(); ++index)
    {
        if (std::abs(positive.values[index] - negative.values[index]) > 1.0e-4F)
        {
            return false;
        }
    }

    // A non-unit but valid quaternion must normalize to the same rotation as its unit form.
    state.orientation = {QuarterTurnAroundZ.x * 2.5F, QuarterTurnAroundZ.y * 2.5F,
                         QuarterTurnAroundZ.z * 2.5F, QuarterTurnAroundZ.w * 2.5F};
    const ModelTransform scaled = ComposeRenderTransform(state, center);
    for (std::size_t index = 0; index < positive.values.size(); ++index)
    {
        if (std::abs(positive.values[index] - scaled.values[index]) > 1.0e-3F)
        {
            return false;
        }
    }
    return true;
}

bool PhysicsRenderSyncPropellerStaysAttached()
{
    const ModelVector3 center = DeepRun::Game::BoundsCenter(OffCenterTestBounds);
    // Body has translated and rotated: the propeller node at (-49, 0, 0) must remain rigidly attached.
    PhysicsBodyState state = IdentityStateAt(center);
    state.position.x += 10.0F;
    state.position.y -= 20.0F;
    state.orientation = QuarterTurnAroundZ;

    const ModelTransform modelToWorld = ComposeRenderTransform(state, center);
    // Exactly what PrepareModelDraws does for the propeller node: body transform * local node transform.
    const ModelTransform propellerDraw = Multiply(modelToWorld, DeepRun::Game::TranslationTransform({-49.0F, 0.0F, 0.0F}));

    const ModelVector3 hullOrigin = TransformPointBy(modelToWorld, {0.0F, 0.0F, 0.0F});
    const ModelVector3 propellerHub = TransformPointBy(propellerDraw, {0.0F, 0.0F, 0.0F});

    // The hub offset from the hull origin must equal the authored local offset rotated by the body rotation:
    // R * (-49, 0, 0) = (0, -49, 0) for a +90 degree Z turn. No drift, no detachment while falling.
    constexpr float Tolerance = 1.0e-3F;
    const ModelVector3 offset{propellerHub.x - hullOrigin.x, propellerHub.y - hullOrigin.y, propellerHub.z - hullOrigin.z};
    return std::abs(offset.x) < Tolerance && std::abs(offset.y + 49.0F) < Tolerance && std::abs(offset.z) < Tolerance;
}

bool PhysicsRenderSyncTransformedBounds()
{
    const ModelBounds& bounds = OffCenterTestBounds; // size (100, 14, 10), center (20, 2, 1)
    constexpr float Tolerance = 1.0e-3F;

    // Translation: position changes, size must not.
    const ModelTransform translation = DeepRun::Game::TranslationTransform({10.0F, -20.0F, 5.0F});
    const auto translated = DeepRun::Game::TransformBounds(bounds, translation);
    if (!translated)
    {
        return false;
    }
    const bool sizePreserved = std::abs((translated->maximum.x - translated->minimum.x) - 100.0F) < Tolerance &&
                               std::abs((translated->maximum.y - translated->minimum.y) - 14.0F) < Tolerance &&
                               std::abs((translated->maximum.z - translated->minimum.z) - 10.0F) < Tolerance;
    // New min = (-30+10, -5-20, -4+5) = (-20, -25, 1); new max = (70+10, 9-20, 6+5) = (80, -11, 11).
    const bool positionMoved = std::abs(translated->minimum.x + 20.0F) < Tolerance &&
                               std::abs(translated->maximum.x - 80.0F) < Tolerance &&
                               std::abs(translated->minimum.y + 25.0F) < Tolerance &&
                               std::abs(translated->maximum.y + 11.0F) < Tolerance;

    // Z rotation by 90 degrees: X and Y extents swap, Z extent is unchanged. All 8 transformed corners must be
    // inside the resulting world AABB by construction (min/max over the corners).
    PhysicsBodyState state = IdentityStateAt(DeepRun::Game::BoundsCenter(bounds));
    state.orientation = QuarterTurnAroundZ;
    const ModelTransform rotation = *DeepRun::Game::BuildBodyToWorld(state);
    const auto rotated = DeepRun::Game::TransformBounds(bounds, rotation);
    if (!rotated)
    {
        return false;
    }
    const bool extentsSwapped = std::abs((rotated->maximum.x - rotated->minimum.x) - 14.0F) < Tolerance &&
                                std::abs((rotated->maximum.y - rotated->minimum.y) - 100.0F) < Tolerance &&
                                std::abs((rotated->maximum.z - rotated->minimum.z) - 10.0F) < Tolerance;

    // Every transformed corner is contained in the world AABB (the defining property of the rebuilt bounds).
    auto element = [](const ModelTransform& m, const std::size_t row, const std::size_t column) noexcept {
        return m.values[column * 4 + row];
    };
    bool cornersContained = true;
    for (const float x : {bounds.minimum.x, bounds.maximum.x})
    {
        for (const float y : {bounds.minimum.y, bounds.maximum.y})
        {
            for (const float z : {bounds.minimum.z, bounds.maximum.z})
            {
                const ModelVector3 corner{
                    element(rotation, 0, 0) * x + element(rotation, 0, 1) * y + element(rotation, 0, 2) * z +
                        element(rotation, 0, 3),
                    element(rotation, 1, 0) * x + element(rotation, 1, 1) * y + element(rotation, 1, 2) * z +
                        element(rotation, 1, 3),
                    element(rotation, 2, 0) * x + element(rotation, 2, 1) * y + element(rotation, 2, 2) * z +
                        element(rotation, 2, 3)};
                if (corner.x < rotated->minimum.x - Tolerance || corner.x > rotated->maximum.x + Tolerance ||
                    corner.y < rotated->minimum.y - Tolerance || corner.y > rotated->maximum.y + Tolerance ||
                    corner.z < rotated->minimum.z - Tolerance || corner.z > rotated->maximum.z + Tolerance)
                {
                    cornersContained = false;
                }
            }
        }
    }
    return sizePreserved && positionMoved && extentsSwapped && cornersContained;
}

bool PhysicsRenderSyncRejectsNonFiniteInput()
{
    const ModelVector3 center = DeepRun::Game::BoundsCenter(OffCenterTestBounds);
    const float nan = std::numeric_limits<float>::quiet_NaN();

    auto nanPosition = IdentityStateAt(center);
    nanPosition.position.y = nan;
    if (DeepRun::Game::BuildBodyToWorld(nanPosition))
    {
        return false;
    }

    auto zeroQuaternion = IdentityStateAt(center);
    zeroQuaternion.orientation = {0.0F, 0.0F, 0.0F, 0.0F};
    if (DeepRun::Game::BuildBodyToWorld(zeroQuaternion))
    {
        return false;
    }

    auto nanQuaternion = IdentityStateAt(center);
    nanQuaternion.orientation.w = nan;
    if (DeepRun::Game::BuildBodyToWorld(nanQuaternion))
    {
        return false;
    }

    // Non-finite bounds or transform must be rejected by TransformBounds as well.
    ModelBounds nonFiniteBounds = OffCenterTestBounds;
    nonFiniteBounds.maximum.z = nan;
    if (DeepRun::Game::TransformBounds(nonFiniteBounds, {}))
    {
        return false;
    }

    ModelTransform nonFiniteTransform{};
    nonFiniteTransform.values[0] = nan;
    return !DeepRun::Game::TransformBounds(OffCenterTestBounds, nonFiniteTransform);
}

bool PhysicsIntegrationSubmarineBodyFallsThroughPivot()
{
    DeepRun::Diagnostics::Logger logger;
    DeepRun::Physics::PhysicsWorld world(logger);
    if (!world.Initialize())
    {
        return false;
    }

    // Submarine-like box body from synthetic (deliberately off-center) bounds, through the public API only.
    const ModelBounds& bounds = OffCenterTestBounds;
    const ModelVector3 center = DeepRun::Game::BoundsCenter(bounds);
    DeepRun::Physics::DynamicBoxBodyCreateInfo info;
    info.halfExtents = {50.0F, 7.0F, 5.0F}; // (max - min) * 0.5 of the synthetic bounds
    info.mass = 12'000'000.0F;              // M2 prototype tuning, same value as the playground
    info.position = {center.x, center.y, center.z};
    info.orientation = {};
    info.gravityEnabled = true;
    const auto handle = world.CreateDynamicBoxBody(info);
    if (!handle.IsValid())
    {
        return false;
    }

    // The render-model Y must follow the physics-body Y exactly through the pivot correction at every sample:
    // modelToWorld translation y == bodyY - centerY, while X/Z stay stable and everything remains finite.
    constexpr float Tolerance = 1.0e-2F;
    for (const int steps : {10, 30, 60})
    {
        for (int step = 0; step < steps; ++step)
        {
            world.Step(1.0F / 60.0F);
        }
        const auto state = world.GetBodyState(handle);
        if (!state || !state->active)
        {
            return false;
        }

        const bool finite = std::isfinite(state->position.x) && std::isfinite(state->position.y) &&
                            std::isfinite(state->position.z) && std::isfinite(state->linearVelocity.x) &&
                            std::isfinite(state->linearVelocity.y) && std::isfinite(state->linearVelocity.z);
        if (!finite || state->orientation.LengthSquared() <= 0.0F)
        {
            return false;
        }

        const ModelTransform modelToWorld = ComposeRenderTransform(*state, center);
        // Identity rotation is preserved (no torque), so the pivot correction is a pure translation offset.
        if (std::abs(modelToWorld.values[13] - (state->position.y - center.y)) > Tolerance)
        {
            return false;
        }
        if (std::abs(state->position.x - center.x) > 0.001F || std::abs(state->position.z - center.z) > 0.001F)
        {
            return false; // X/Z must remain stable under pure gravity
        }
    }

    const auto finalState = world.GetBodyState(handle);
    if (!finalState)
    {
        return false;
    }
    return finalState->position.y < center.y && finalState->linearVelocity.y < 0.0F;
}

// ---------------------------------------------------------------------------
// M2 Slice C2.1: 2.5D rigid-body DOF contract (generic physics API) and the
// corrected world-bounds composition (bodyToWorld * modelToBody).
// ---------------------------------------------------------------------------

bool StateIsFinite(const DeepRun::Physics::PhysicsBodyState& state)
{
    return std::isfinite(state.position.x) && std::isfinite(state.position.y) && std::isfinite(state.position.z) &&
           std::isfinite(state.orientation.x) && std::isfinite(state.orientation.y) && std::isfinite(state.orientation.z) &&
           std::isfinite(state.orientation.w) && std::isfinite(state.linearVelocity.x) &&
           std::isfinite(state.linearVelocity.y) && std::isfinite(state.linearVelocity.z) &&
           std::isfinite(state.angularVelocity.x) && std::isfinite(state.angularVelocity.y) &&
           std::isfinite(state.angularVelocity.z);
}

// M2-like planar body: XY translation + Z rotation allowed, everything else locked (the exact Game-layer
// configuration of the canonical submarine). Gravity on, zero damping, finite initial velocities only.
DeepRun::Physics::DynamicBoxBodyCreateInfo PlanarM2LikeBodyInfo()
{
    DeepRun::Physics::DynamicBoxBodyCreateInfo info = ValidBoxBodyInfo();
    info.position = {0.0F, 5.0F, 0.0F};
    info.orientation = {}; // identity
    info.gravityEnabled = true;
    info.linearDamping = 0.0F;
    info.angularDamping = 0.0F;
    info.initialLinearVelocity = {3.0F, 0.0F, 0.0F};
    info.initialAngularVelocity = {0.0F, 0.0F, 1.0F}; // rad/s around +Z only

    DeepRun::Physics::PhysicsDegreesOfFreedom dof;
    dof.translationX = true;   // M2 gameplay plane: XY translation ...
    dof.translationY = true;
    dof.translationZ = false;  // ... locked along Z (toward camera)
    dof.rotationX = false;     // no roll around X
    dof.rotationY = false;     // no yaw around Y
    dof.rotationZ = true;      // pitch nose up/down
    info.degreesOfFreedom = dof;
    return info;
}

bool DefaultBodyKeepsAllSixDOFs()
{
    DeepRun::Diagnostics::Logger logger;
    DeepRun::Physics::PhysicsWorld world(logger);
    if (!world.Initialize())
    {
        return false;
    }

    // Generic body without an explicit restriction: the default contract must keep all six DOFs available,
    // exactly as in C1. Every axis gets a distinct initial velocity so any silently locked axis would show up.
    auto info = ValidBoxBodyInfo();
    info.position = {0.0F, 5.0F, 0.0F};
    info.gravityEnabled = true;
    info.linearDamping = 0.0F;
    info.angularDamping = 0.0F;
    info.initialLinearVelocity = {10.0F, -2.0F, 5.0F};
    info.initialAngularVelocity = {1.5F, 2.0F, 3.0F};
    const auto handle = world.CreateDynamicBoxBody(info);
    const auto initial = world.GetBodyState(handle);
    if (!handle.IsValid() || !initial)
    {
        return false;
    }

    constexpr int Steps = 60; // one second at the fixed step
    for (int step = 0; step < Steps; ++step)
    {
        world.Step(1.0F / 60.0F);
    }
    const auto finalState = world.GetBodyState(handle);
    if (!finalState || !finalState->active)
    {
        return false;
    }

    // Every DOF must actually be usable: X/Z translation at the supplied speed (no hidden drag), Y falling
    // under gravity, rotation evolving on all three axes with the angular velocity preserved (zero damping).
    const bool xMoved = std::abs(finalState->position.x - initial->position.x) > 9.0F; // ~10 m in one second
    const bool zMoved = std::abs(finalState->position.z - initial->position.z) > 4.0F; // ~5 m in one second
    const bool yFell = finalState->position.y < initial->position.y - 2.0F;            // gravity over one second
    const bool rotated = !DeepRun::Physics::PhysicsQuaternion::SameRotation(initial->orientation,
                                                                            finalState->orientation);
    const bool angularPreserved = std::abs(finalState->angularVelocity.x - 1.5F) < 0.2F &&
                                  std::abs(finalState->angularVelocity.y - 2.0F) < 0.2F &&
                                  std::abs(finalState->angularVelocity.z - 3.0F) < 0.2F;
    return StateIsFinite(*finalState) && xMoved && zMoved && yFell && rotated && angularPreserved;
}

bool PlanarBodyStaysInGameplayPlane()
{
    DeepRun::Diagnostics::Logger logger;
    DeepRun::Physics::PhysicsWorld world(logger);
    if (!world.Initialize())
    {
        return false;
    }

    const auto info = PlanarM2LikeBodyInfo();
    const auto handle = world.CreateDynamicBoxBody(info);
    const auto initial = world.GetBodyState(handle);
    if (!handle.IsValid() || !initial)
    {
        return false;
    }

    // The creation state must already show the locked-DOF contract: no Z linear velocity and no X/Y angular
    // velocity, while the allowed components are exactly what was supplied.
    constexpr float Tolerance = 1.0e-5F;
    if (std::abs(initial->linearVelocity.z) > Tolerance || std::abs(initial->angularVelocity.x) > Tolerance ||
        std::abs(initial->angularVelocity.y) > Tolerance ||
        std::abs(initial->linearVelocity.x - 3.0F) > Tolerance ||
        std::abs(initial->angularVelocity.z - 1.0F) > Tolerance)
    {
        return false;
    }

    constexpr int Steps = 60; // one second at the fixed step
    for (int step = 0; step < Steps; ++step)
    {
        world.Step(1.0F / 60.0F);
    }
    const auto finalState = world.GetBodyState(handle);
    if (!finalState || !finalState->active)
    {
        return false;
    }

    // Locked DOFs must not develop after simulation: no Z drift, and a pure Z rotation keeps the quaternion in
    // (0, 0, sin(a/2), cos(a/2)) form with zero X/Y angular velocity.
    const bool zLocked = std::abs(finalState->position.z - initial->position.z) < 1.0e-3F &&
                         std::abs(finalState->linearVelocity.z) < 1.0e-4F;
    const bool rollYawLocked = std::abs(finalState->orientation.x) < 1.0e-5F &&
                               std::abs(finalState->orientation.y) < 1.0e-5F &&
                               std::abs(finalState->angularVelocity.x) < 1.0e-4F &&
                               std::abs(finalState->angularVelocity.y) < 1.0e-4F;

    // The allowed DOFs must NOT be blocked: Y falls under gravity, X moves at the supplied speed...
    const bool yFell = finalState->position.y < initial->position.y - 2.0F && finalState->linearVelocity.y < 0.0F;
    const bool xMoved = std::abs(finalState->position.x - (initial->position.x + 3.0F)) < 0.1F;
    // ... and Z rotation evolves with the supplied angular velocity around Z (~one radian after one second).
    const bool zRotated = !DeepRun::Physics::PhysicsQuaternion::SameRotation(initial->orientation,
                                                                             finalState->orientation) &&
                          std::abs(finalState->angularVelocity.z - 1.0F) < 0.05F;

    return StateIsFinite(*finalState) && zLocked && rollYawLocked && yFell && xMoved && zRotated;
}

bool PlanarBodyRejectsLockedAxisInitialVelocity()
{
    DeepRun::Diagnostics::Logger logger;
    DeepRun::Physics::PhysicsWorld world(logger);
    if (!world.Initialize())
    {
        return false;
    }

    const auto info = PlanarM2LikeBodyInfo();
    DeepRun::Physics::PhysicsError error;

    // Locked translation axis: an initial Z velocity must be rejected as recoverable input.
    auto zVelocity = info;
    zVelocity.initialLinearVelocity = {0.0F, 0.0F, 5.0F};
    if (world.CreateDynamicBoxBody(zVelocity, &error).IsValid() ||
        error.code != DeepRun::Physics::PhysicsErrorCode::InvalidInput)
    {
        return false;
    }

    // Locked rotation axes: initial X and Y angular velocities must be rejected.
    auto xAngular = info;
    xAngular.initialAngularVelocity = {2.0F, 0.0F, 0.0F};
    if (world.CreateDynamicBoxBody(xAngular, &error).IsValid() ||
        error.code != DeepRun::Physics::PhysicsErrorCode::InvalidInput)
    {
        return false;
    }
    auto yAngular = info;
    yAngular.initialAngularVelocity = {0.0F, 3.0F, 0.0F};
    if (world.CreateDynamicBoxBody(yAngular, &error).IsValid() ||
        error.code != DeepRun::Physics::PhysicsErrorCode::InvalidInput)
    {
        return false;
    }

    // Locking every DOF is not a valid dynamic body: that is the static-body case.
    auto allLocked = info;
    DeepRun::Physics::PhysicsDegreesOfFreedom noneAllowed;
    noneAllowed.translationX = noneAllowed.translationY = noneAllowed.translationZ = false;
    noneAllowed.rotationX = noneAllowed.rotationY = noneAllowed.rotationZ = false;
    allLocked.degreesOfFreedom = noneAllowed;
    if (world.CreateDynamicBoxBody(allLocked, &error).IsValid() ||
        error.code != DeepRun::Physics::PhysicsErrorCode::InvalidInput)
    {
        return false;
    }

    // Control: allowed-axis velocities are still accepted.
    const auto handle = world.CreateDynamicBoxBody(info);
    return handle.IsValid() && world.GetBodyState(handle).has_value();
}

// ---------------------------------------------------------------------------
// M2 Slice E1: generic force-at-world-position PhysicsWorld API (headless, public API only)
// ---------------------------------------------------------------------------

// Centered dynamic box for force tests: gravity off, zero damping, known mass, identity orientation at the
// world origin so cross-product math is clean in world space. Default DOF = all six allowed (full 6-DOF).
DeepRun::Physics::DynamicBoxBodyCreateInfo ForceTestBodyInfo()
{
    DeepRun::Physics::DynamicBoxBodyCreateInfo info;
    info.halfExtents = {1.0F, 1.0F, 1.0F}; // unit cube: symmetric inertia about every axis
    info.mass = 2.0F;                       // known mass for deltaV ~= F/m*dt checks
    info.position = {0.0F, 0.0F, 0.0F};     // center of mass at the world origin
    info.orientation = {};                  // identity rotation
    info.gravityEnabled = false;            // isolate force effects from gravity
    info.linearDamping = 0.0F;              // no hidden drag
    info.angularDamping = 0.0F;
    info.initialLinearVelocity = {0.0F, 0.0F, 0.0F};
    info.initialAngularVelocity = {0.0F, 0.0F, 0.0F};
    return info;
}

// A: centered force -> pure translation, no spin, deltaV ~= F/m*dt (non bit-perfect tolerance).
bool ForceCenteredProducesTranslation()
{
    DeepRun::Diagnostics::Logger logger;
    DeepRun::Physics::PhysicsWorld world(logger);
    if (!world.Initialize())
    {
        return false;
    }

    const auto handle = world.CreateDynamicBoxBody(ForceTestBodyInfo());
    const auto initial = world.GetBodyState(handle);
    if (!handle.IsValid() || !initial)
    {
        return false;
    }

    constexpr float ForceX = 10.0F; // Newtons along +X
    constexpr float Mass = 2.0F;    // matches ForceTestBodyInfo
    constexpr float Dt = 1.0F / 60.0F;
    const DeepRun::Physics::PhysicsVector3 com{initial->position.x, initial->position.y, initial->position.z};

    if (!world.AddForceAtWorldPosition(handle, {ForceX, 0.0F, 0.0F}, com))
    {
        return false;
    }
    world.Step(Dt);

    const auto after = world.GetBodyState(handle);
    if (!after)
    {
        return false;
    }

    const bool movedForward = after->linearVelocity.x > 0.0F && after->position.x > initial->position.x;
    const bool noSpin = std::abs(after->angularVelocity.x) < 1e-3F && std::abs(after->angularVelocity.y) < 1e-3F &&
                        std::abs(after->angularVelocity.z) < 1e-3F;

    // deltaV ~= F/m*dt with a reasonable (not bit-perfect) tolerance.
    const float expectedDeltaV = ForceX / Mass * Dt;
    const bool deltaVMatches = std::abs(after->linearVelocity.x - expectedDeltaV) <= 0.2F * expectedDeltaV + 1e-4F;

    return movedForward && noSpin && deltaVMatches && StateIsFinite(*after);
}

// B: off-center force -> linear motion plus torque about the cross-product axis (the key E1 correctness test).
bool ForceOffCenterProducesTorque()
{
    DeepRun::Diagnostics::Logger logger;
    DeepRun::Physics::PhysicsWorld world(logger);
    if (!world.Initialize())
    {
        return false;
    }

    const auto handle = world.CreateDynamicBoxBody(ForceTestBodyInfo());
    const auto initial = world.GetBodyState(handle);
    if (!handle.IsValid() || !initial)
    {
        return false;
    }

    constexpr float ForceY = 10.0F; // Newtons along +Y
    constexpr float OffsetX = 2.0F; // application point offset from the center of mass along +X (meters)
    constexpr int Steps = 30;       // half a second at the fixed step

    for (int step = 0; step < Steps; ++step)
    {
        const auto state = world.GetBodyState(handle);
        if (!state)
        {
            return false;
        }
        // Keep the application point a constant +X offset from the CURRENT center of mass so the lever arm stays
        // (OffsetX, 0, 0) in world space and the torque is a steady r x F = +Z.
        const DeepRun::Physics::PhysicsVector3 point{state->position.x + OffsetX, state->position.y, state->position.z};
        if (!world.AddForceAtWorldPosition(handle, {0.0F, ForceY, 0.0F}, point))
        {
            return false;
        }
        world.Step(1.0F / 60.0F);
    }

    const auto after = world.GetBodyState(handle);
    if (!after)
    {
        return false;
    }

    // r=(+X), F=+Y -> torque +Z: linear +Y motion and positive Z spin, with no X/Y spin.
    const bool linearUp = after->linearVelocity.y > 0.0F && after->position.y > initial->position.y;
    const bool spinsAroundZ = after->angularVelocity.z > 0.1F;
    const bool noOtherSpin = std::abs(after->angularVelocity.x) < 1e-3F && std::abs(after->angularVelocity.y) < 1e-3F;
    const bool orientationRotated = !DeepRun::Physics::PhysicsQuaternion::SameRotation(initial->orientation, after->orientation);

    return linearUp && spinsAroundZ && noOtherSpin && orientationRotated && StateIsFinite(*after);
}

// C: same force at the opposite application point flips the torque sign (world-position / cross-product convention).
bool ForceOppositePointFlipsTorqueSign()
{
    DeepRun::Diagnostics::Logger logger;
    DeepRun::Physics::PhysicsWorld world(logger);
    if (!world.Initialize())
    {
        return false;
    }

    const auto handle = world.CreateDynamicBoxBody(ForceTestBodyInfo());
    const auto initial = world.GetBodyState(handle);
    if (!handle.IsValid() || !initial)
    {
        return false;
    }

    constexpr float ForceY = 10.0F;
    constexpr float OffsetX = -2.0F; // application point offset along -X (opposite of test B)
    constexpr int Steps = 30;

    for (int step = 0; step < Steps; ++step)
    {
        const auto state = world.GetBodyState(handle);
        if (!state)
        {
            return false;
        }
        const DeepRun::Physics::PhysicsVector3 point{state->position.x + OffsetX, state->position.y, state->position.z};
        if (!world.AddForceAtWorldPosition(handle, {0.0F, ForceY, 0.0F}, point))
        {
            return false;
        }
        world.Step(1.0F / 60.0F);
    }

    const auto after = world.GetBodyState(handle);
    if (!after)
    {
        return false;
    }

    // r=(-X), F=+Y -> torque -Z: still linear +Y motion, but the Z spin is now negative.
    const bool linearUp = after->linearVelocity.y > 0.0F && after->position.y > initial->position.y;
    const bool spinsNegativeZ = after->angularVelocity.z < -0.1F;
    const bool noOtherSpin = std::abs(after->angularVelocity.x) < 1e-3F && std::abs(after->angularVelocity.y) < 1e-3F;

    return linearUp && spinsNegativeZ && noOtherSpin && StateIsFinite(*after);
}

// D: two equal opposite-point forces in the SAME step cancel torque but add linear force (multi-call accumulation).
bool ForceSymmetricPointsCancelTorque()
{
    DeepRun::Diagnostics::Logger logger;
    DeepRun::Physics::PhysicsWorld world(logger);
    if (!world.Initialize())
    {
        return false;
    }

    const auto handle = world.CreateDynamicBoxBody(ForceTestBodyInfo());
    const auto initial = world.GetBodyState(handle);
    if (!handle.IsValid() || !initial)
    {
        return false;
    }

    constexpr float ForceY = 10.0F;
    constexpr float OffsetX = 2.0F;
    constexpr int Steps = 30;

    for (int step = 0; step < Steps; ++step)
    {
        const auto state = world.GetBodyState(handle);
        if (!state)
        {
            return false;
        }
        // Both calls land in the same fixed step: Jolt accumulates them before Step integrates.
        const DeepRun::Physics::PhysicsVector3 plus{state->position.x + OffsetX, state->position.y, state->position.z};
        const DeepRun::Physics::PhysicsVector3 minus{state->position.x - OffsetX, state->position.y, state->position.z};
        if (!world.AddForceAtWorldPosition(handle, {0.0F, ForceY, 0.0F}, plus) ||
            !world.AddForceAtWorldPosition(handle, {0.0F, ForceY, 0.0F}, minus))
        {
            return false;
        }
        world.Step(1.0F / 60.0F);
    }

    const auto after = world.GetBodyState(handle);
    if (!after)
    {
        return false;
    }

    // Equal +Y forces at +/-X: net linear force (0, 2*ForceY, 0), net torque zero -> Y motion, no Z spin.
    const bool linearUp = after->linearVelocity.y > 0.0F && after->position.y > initial->position.y;
    const bool torqueCancelled = std::abs(after->angularVelocity.z) < 1e-3F &&
                                 std::abs(after->angularVelocity.x) < 1e-3F &&
                                 std::abs(after->angularVelocity.y) < 1e-3F;

    return linearUp && torqueCancelled && StateIsFinite(*after);
}

// E: two separate AddForce(F) calls before one Step equal a single AddForce(2F) call (accumulation semantics).
bool ForceMultipleCallsAccumulate()
{
    DeepRun::Diagnostics::Logger logger;
    DeepRun::Physics::PhysicsWorld world(logger);
    if (!world.Initialize())
    {
        return false;
    }

    auto infoA = ForceTestBodyInfo(); // at the origin
    auto infoB = ForceTestBodyInfo();
    infoB.position = {100.0F, 0.0F, 0.0F}; // far away: the two test bodies never contact each other
    const auto handleA = world.CreateDynamicBoxBody(infoA);
    const auto handleB = world.CreateDynamicBoxBody(infoB);
    const auto stateA = world.GetBodyState(handleA);
    const auto stateB = world.GetBodyState(handleB);
    if (!handleA.IsValid() || !handleB.IsValid() || !stateA || !stateB)
    {
        return false;
    }

    constexpr float ForceX = 10.0F; // Newtons along +X
    constexpr float Mass = 2.0F;
    constexpr float Dt = 1.0F / 60.0F;

    const DeepRun::Physics::PhysicsVector3 comA{stateA->position.x, stateA->position.y, stateA->position.z};
    const DeepRun::Physics::PhysicsVector3 comB{stateB->position.x, stateB->position.y, stateB->position.z};

    // Body A: two calls of F at its COM. Body B: one call of 2F at its COM. Both total 2F before the step.
    if (!world.AddForceAtWorldPosition(handleA, {ForceX, 0.0F, 0.0F}, comA) ||
        !world.AddForceAtWorldPosition(handleA, {ForceX, 0.0F, 0.0F}, comA) ||
        !world.AddForceAtWorldPosition(handleB, {2.0F * ForceX, 0.0F, 0.0F}, comB))
    {
        return false;
    }
    world.Step(Dt); // one shared step integrates both bodies' accumulated forces

    const auto afterA = world.GetBodyState(handleA);
    const auto afterB = world.GetBodyState(handleB);
    if (!afterA || !afterB)
    {
        return false;
    }

    const float expectedDeltaV = 2.0F * ForceX / Mass * Dt; // both bodies should reach this X velocity
    const bool aMatchesExpected = std::abs(afterA->linearVelocity.x - expectedDeltaV) <= 0.2F * expectedDeltaV + 1e-4F;
    const bool bMatchesExpected = std::abs(afterB->linearVelocity.x - expectedDeltaV) <= 0.2F * expectedDeltaV + 1e-4F;
    const bool aEqualsB = std::abs(afterA->linearVelocity.x - afterB->linearVelocity.x) < 1e-3F;

    return aMatchesExpected && bMatchesExpected && aEqualsB && StateIsFinite(*afterA) && StateIsFinite(*afterB);
}

// F: the force is per-step, not persistent — stepping again without re-applying it must NOT add another increment.
bool ForceIsNotPersistent()
{
    DeepRun::Diagnostics::Logger logger;
    DeepRun::Physics::PhysicsWorld world(logger);
    if (!world.Initialize())
    {
        return false;
    }

    const auto handle = world.CreateDynamicBoxBody(ForceTestBodyInfo());
    const auto initial = world.GetBodyState(handle);
    if (!handle.IsValid() || !initial)
    {
        return false;
    }

    constexpr float ForceX = 10.0F;
    constexpr float Mass = 2.0F;
    constexpr float Dt = 1.0F / 60.0F;
    const DeepRun::Physics::PhysicsVector3 com{initial->position.x, initial->position.y, initial->position.z};

    if (!world.AddForceAtWorldPosition(handle, {ForceX, 0.0F, 0.0F}, com))
    {
        return false;
    }
    world.Step(Dt); // integrates the one applied force -> V1

    const auto afterFirst = world.GetBodyState(handle);
    if (!afterFirst)
    {
        return false;
    }
    const float v1 = afterFirst->linearVelocity.x;
    const float positionAfterFirst = afterFirst->position.x;
    if (v1 <= 0.0F)
    {
        return false; // sanity: the first step must have produced forward velocity
    }

    world.Step(Dt); // second step with NO force applied

    const auto afterSecond = world.GetBodyState(handle);
    if (!afterSecond)
    {
        return false;
    }

    // Velocity stays ~V1 (no second F/m*dt increment), while position keeps advancing from V1.
    const bool velocityHeld = std::abs(afterSecond->linearVelocity.x - v1) <= 0.1F * v1 + 1e-4F;
    const bool notReApplied = afterSecond->linearVelocity.x < v1 + 0.5F * (ForceX / Mass * Dt);
    const bool positionAdvanced = afterSecond->position.x > positionAfterFirst;

    return velocityHeld && notReApplied && positionAdvanced && StateIsFinite(*afterSecond);
}

// G: C2.1 planar DOFs stay authoritative — a force that would create forbidden motion in full 6-DOF is blocked,
// while allowed translation/pitch still work. The force is NOT manually projected; Jolt restricts the result.
bool ForcePlanarDOFPreservation()
{
    DeepRun::Diagnostics::Logger logger;
    DeepRun::Physics::PhysicsWorld world(logger);
    if (!world.Initialize())
    {
        return false;
    }

    auto info = ValidBoxBodyInfo();
    info.position = {0.0F, 0.0F, 0.0F};
    info.orientation = {}; // identity: clean world-space cross products
    info.gravityEnabled = false;
    info.linearDamping = 0.0F;
    info.angularDamping = 0.0F;
    info.initialLinearVelocity = {0.0F, 0.0F, 0.0F};
    info.initialAngularVelocity = {0.0F, 0.0F, 0.0F};

    DeepRun::Physics::PhysicsDegreesOfFreedom dof; // C2.1 gameplay plane: XY translation + Z rotation only
    dof.translationX = true;
    dof.translationY = true;
    dof.translationZ = false;
    dof.rotationX = false;
    dof.rotationY = false;
    dof.rotationZ = true;
    info.degreesOfFreedom = dof;

    const auto handle = world.CreateDynamicBoxBody(info);
    const auto initial = world.GetBodyState(handle);
    if (!handle.IsValid() || !initial)
    {
        return false;
    }

    constexpr float ForceY = 10.0F;
    constexpr float OffsetX = 2.0F; // allowed lever: +X offset -> Z pitch (rotationZ allowed)
    constexpr float OffsetZ = 2.0F; // forbidden lever: +Z offset -> X roll torque (rotationX locked, must be blocked)
    constexpr int Steps = 30;

    for (int step = 0; step < Steps; ++step)
    {
        const auto state = world.GetBodyState(handle);
        if (!state)
        {
            return false;
        }
        // Allowed: +Y force at COM+(+X) -> Y translation + Z pitch.
        const DeepRun::Physics::PhysicsVector3 allowedPoint{state->position.x + OffsetX, state->position.y, state->position.z};
        // Forbidden: +Y force at COM+(+Z) -> would create X (roll) torque in a full 6-DOF body; Jolt must block it.
        const DeepRun::Physics::PhysicsVector3 forbiddenPoint{state->position.x, state->position.y, state->position.z + OffsetZ};
        if (!world.AddForceAtWorldPosition(handle, {0.0F, ForceY, 0.0F}, allowedPoint) ||
            !world.AddForceAtWorldPosition(handle, {0.0F, ForceY, 0.0F}, forbiddenPoint))
        {
            return false;
        }
        world.Step(1.0F / 60.0F);
    }

    const auto after = world.GetBodyState(handle);
    if (!after)
    {
        return false;
    }

    // Locked DOFs stay locked: no Z translation drift, and the forbidden X roll (and Y yaw) never develops.
    const bool zLocked = std::abs(after->position.z - initial->position.z) < 1e-3F &&
                         std::abs(after->linearVelocity.z) < 1e-4F;
    const bool rollYawBlocked = std::abs(after->angularVelocity.x) < 1e-3F && std::abs(after->angularVelocity.y) < 1e-3F;

    // Allowed DOFs still respond: net +Y force -> Y motion, and the +X lever -> positive Z pitch.
    const bool yMotion = after->linearVelocity.y > 0.0F && after->position.y > initial->position.y;
    const bool zPitch = after->angularVelocity.z > 0.1F;

    return zLocked && rollYawBlocked && yMotion && zPitch && StateIsFinite(*after);
}

// H: invalid inputs are recoverable errors (no crash, no state mutation); zero force is a valid no-op.
bool ForceInvalidInputAndZeroNoOp()
{
    DeepRun::Diagnostics::Logger logger;
    DeepRun::Physics::PhysicsWorld world(logger);
    if (!world.Initialize())
    {
        return false;
    }

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    const DeepRun::Physics::PhysicsVector3 force{10.0F, 0.0F, 0.0F};
    const DeepRun::Physics::PhysicsVector3 point{0.0F, 0.0F, 0.0F};

    // A second PhysicsWorld cannot initialize in the same process (Jolt is a single global instance), so the
    // foreign-handle case is covered by the existing "Physics handle semantics" test; here we cover the
    // invalid and stale cases against this world's own resolution path.
    DeepRun::Physics::PhysicsError error;
    const DeepRun::Physics::PhysicsBodyHandle defaultHandle; // never created: invalid by construction

    if (world.AddForceAtWorldPosition(defaultHandle, force, point, &error) ||
        error.code != DeepRun::Physics::PhysicsErrorCode::InvalidHandle)
    {
        return false;
    }

    // Stale handle: created and destroyed in this world.
    const auto stale = world.CreateDynamicBoxBody(ForceTestBodyInfo());
    if (!stale.IsValid() || !world.DestroyBody(stale))
    {
        return false;
    }
    if (world.AddForceAtWorldPosition(stale, force, point, &error) ||
        error.code != DeepRun::Physics::PhysicsErrorCode::InvalidHandle)
    {
        return false;
    }

    // Non-finite force / position on a VALID live handle: rejected as InvalidInput.
    const auto handle = world.CreateDynamicBoxBody(ForceTestBodyInfo());
    if (!handle.IsValid())
    {
        return false;
    }
    const DeepRun::Physics::PhysicsVector3 nanForce{nan, 0.0F, 0.0F};
    const DeepRun::Physics::PhysicsVector3 infForce{infinity, 0.0F, 0.0F};
    const DeepRun::Physics::PhysicsVector3 negInfForce{-infinity, 0.0F, 0.0F};
    const DeepRun::Physics::PhysicsVector3 nanPoint{nan, 0.0F, 0.0F};
    const DeepRun::Physics::PhysicsVector3 infPoint{infinity, 0.0F, 0.0F};
    const DeepRun::Physics::PhysicsVector3 negInfPoint{-infinity, 0.0F, 0.0F};

    for (const auto& badForce : {nanForce, infForce, negInfForce})
    {
        if (world.AddForceAtWorldPosition(handle, badForce, point, &error) ||
            error.code != DeepRun::Physics::PhysicsErrorCode::InvalidInput)
        {
            return false;
        }
    }
    for (const auto& badPoint : {nanPoint, infPoint, negInfPoint})
    {
        if (world.AddForceAtWorldPosition(handle, force, badPoint, &error) ||
            error.code != DeepRun::Physics::PhysicsErrorCode::InvalidInput)
        {
            return false;
        }
    }

    // No state mutation from all the rejections above: the body is still exactly at rest.
    const auto beforeZero = world.GetBodyState(handle);
    if (!beforeZero || !StateIsFinite(*beforeZero) ||
        std::abs(beforeZero->linearVelocity.x) > 1e-6F || std::abs(beforeZero->position.x) > 1e-6F)
    {
        return false;
    }

    // Zero force is a valid no-op: accepted, and a subsequent force-free step leaves the body at rest.
    const DeepRun::Physics::PhysicsVector3 zero{0.0F, 0.0F, 0.0F};
    if (!world.AddForceAtWorldPosition(handle, zero, point))
    {
        return false; // zero force must succeed (not be rejected as malformed)
    }
    world.Step(1.0F / 60.0F);
    const auto afterZero = world.GetBodyState(handle);
    if (!afterZero || !StateIsFinite(*afterZero))
    {
        return false;
    }
    return std::abs(afterZero->linearVelocity.x) < 1e-6F && std::abs(afterZero->position.x) < 1e-6F;
}

// I: a sleeping dynamic body is activated by a non-zero force (standard Jolt behaviour, observed via public API).
bool ForceActivatesSleepingBody()
{
    DeepRun::Diagnostics::Logger logger;
    DeepRun::Physics::PhysicsWorld world(logger);
    if (!world.Initialize())
    {
        return false;
    }

    const auto handle = world.CreateDynamicBoxBody(ForceTestBodyInfo());
    if (!handle.IsValid())
    {
        return false;
    }

    // Let the stationary, force-free body settle and sleep (Jolt default TimeBeforeSleep is 0.5 s).
    constexpr int SettleSteps = 120; // two seconds at the fixed step
    for (int step = 0; step < SettleSteps; ++step)
    {
        world.Step(1.0F / 60.0F);
    }
    const auto sleeping = world.GetBodyState(handle);
    if (!sleeping || sleeping->active)
    {
        return false; // the body must have gone to sleep before we test activation
    }

    constexpr float ForceX = 10.0F;
    const DeepRun::Physics::PhysicsVector3 com{sleeping->position.x, sleeping->position.y, sleeping->position.z};
    if (!world.AddForceAtWorldPosition(handle, {ForceX, 0.0F, 0.0F}, com))
    {
        return false;
    }
    world.Step(1.0F / 60.0F);

    const auto after = world.GetBodyState(handle);
    if (!after)
    {
        return false;
    }

    // The non-zero force woke the body and produced motion in one step.
    return after->active && after->linearVelocity.x > 0.0F && after->position.x > sleeping->position.x &&
           StateIsFinite(*after);
}

bool WorldBoundsUseModelToWorldComposition()
{
    const ModelBounds& bounds = OffCenterTestBounds; // size (100, 14, 10), center (20, 2, 1): off-center on purpose
    constexpr float Tolerance = 1.0e-3F;
    const ModelVector3 center = DeepRun::Game::BoundsCenter(bounds);

    // The exact per-frame flow of PhysicalPlayground::Render: body snapshot -> bodyToWorld -> modelToWorld,
    // and that single matrix feeds BOTH the draw preparation and the rendered world bounds.
    auto compose = [&center](const PhysicsBodyState& state) -> std::expected<ModelTransform, std::string> {
        const auto bodyToWorld = DeepRun::Game::BuildBodyToWorld(state);
        if (!bodyToWorld)
        {
            return std::unexpected(bodyToWorld.error());
        }
        return Multiply(*bodyToWorld, DeepRun::Game::TranslationTransform({-center.x, -center.y, -center.z}));
    };

    // Initial body: position = boundsCenter, orientation = identity -> modelToWorld is exactly identity, so the
    // rendered world bounds must equal the original model bounds. Under the C2 implementation (bounds passed
    // through bodyToWorld alone) this would be shifted by +center and fail.
    const PhysicsBodyState initial = IdentityStateAt(center);
    const auto initialModelToWorld = compose(initial);
    if (!initialModelToWorld)
    {
        return false;
    }
    const auto initialWorldBounds = DeepRun::Game::TransformBounds(bounds, *initialModelToWorld);
    if (!initialWorldBounds)
    {
        return false;
    }
    const bool identityMatchesOriginal =
        std::abs(initialWorldBounds->minimum.x - bounds.minimum.x) < Tolerance &&
        std::abs(initialWorldBounds->minimum.y - bounds.minimum.y) < Tolerance &&
        std::abs(initialWorldBounds->minimum.z - bounds.minimum.z) < Tolerance &&
        std::abs(initialWorldBounds->maximum.x - bounds.maximum.x) < Tolerance &&
        std::abs(initialWorldBounds->maximum.y - bounds.maximum.y) < Tolerance &&
        std::abs(initialWorldBounds->maximum.z - bounds.maximum.z) < Tolerance;

    // Translation: the rendered world bounds must shift by exactly the same displacement as the rendered model.
    PhysicsBodyState translated = initial;
    translated.position.x += 10.0F;
    translated.position.y -= 20.0F;
    const auto translatedModelToWorld = compose(translated);
    if (!translatedModelToWorld)
    {
        return false;
    }
    const auto translatedWorldBounds = DeepRun::Game::TransformBounds(bounds, *translatedModelToWorld);
    if (!translatedWorldBounds)
    {
        return false;
    }
    // The model's pivot point (bounds center in model space) maps to the body position: displacement (+10, -20).
    const ModelVector3 renderedPivot = TransformPointBy(*translatedModelToWorld, center);
    const bool pivotMovedExactly = std::abs(renderedPivot.x - (center.x + 10.0F)) < Tolerance &&
                                   std::abs(renderedPivot.y - (center.y - 20.0F)) < Tolerance &&
                                   std::abs(renderedPivot.z - center.z) < Tolerance;
    // The world bounds shift by exactly the same vector: min/max of the original bounds plus (+10, -20, 0).
    const bool boundsShiftedExactly =
        std::abs(translatedWorldBounds->minimum.x - (bounds.minimum.x + 10.0F)) < Tolerance &&
        std::abs(translatedWorldBounds->minimum.y - (bounds.minimum.y - 20.0F)) < Tolerance &&
        std::abs(translatedWorldBounds->minimum.z - bounds.minimum.z) < Tolerance &&
        std::abs(translatedWorldBounds->maximum.x - (bounds.maximum.x + 10.0F)) < Tolerance &&
        std::abs(translatedWorldBounds->maximum.y - (bounds.maximum.y - 20.0F)) < Tolerance &&
        std::abs(translatedWorldBounds->maximum.z - bounds.maximum.z) < Tolerance;

    // Z rotation: the world AABB must match bodyToWorld * modelToBody and contain every transformed corner of
    // the SAME matrix used for drawing. A +90 degree turn about Z swaps the X/Y extents (100 x 14 -> 14 x 100)
    // around the pivot at center, leaving Z untouched: [13, 27] x [-48, 52] x [-4, 6].
    PhysicsBodyState rotated = initial;
    rotated.orientation = QuarterTurnAroundZ;
    const auto rotatedModelToWorld = compose(rotated);
    if (!rotatedModelToWorld)
    {
        return false;
    }
    const auto rotatedWorldBounds = DeepRun::Game::TransformBounds(bounds, *rotatedModelToWorld);
    if (!rotatedWorldBounds)
    {
        return false;
    }
    const bool extentsSwapped = std::abs(rotatedWorldBounds->minimum.x - 13.0F) < Tolerance &&
                                std::abs(rotatedWorldBounds->maximum.x - 27.0F) < Tolerance &&
                                std::abs(rotatedWorldBounds->minimum.y + 48.0F) < Tolerance &&
                                std::abs(rotatedWorldBounds->maximum.y - 52.0F) < Tolerance &&
                                std::abs(rotatedWorldBounds->minimum.z - bounds.minimum.z) < Tolerance &&
                                std::abs(rotatedWorldBounds->maximum.z - bounds.maximum.z) < Tolerance;

    bool cornersContained = true;
    for (const float x : {bounds.minimum.x, bounds.maximum.x})
    {
        for (const float y : {bounds.minimum.y, bounds.maximum.y})
        {
            for (const float z : {bounds.minimum.z, bounds.maximum.z})
            {
                const ModelVector3 corner = TransformPointBy(*rotatedModelToWorld, {x, y, z});
                if (corner.x < rotatedWorldBounds->minimum.x - Tolerance ||
                    corner.x > rotatedWorldBounds->maximum.x + Tolerance ||
                    corner.y < rotatedWorldBounds->minimum.y - Tolerance ||
                    corner.y > rotatedWorldBounds->maximum.y + Tolerance ||
                    corner.z < rotatedWorldBounds->minimum.z - Tolerance ||
                    corner.z > rotatedWorldBounds->maximum.z + Tolerance)
                {
                    cornersContained = false;
                }
            }
        }
    }

    return identityMatchesOriginal && pivotMovedExactly && boundsShiftedExactly && extentsSwapped &&
           cornersContained;
}

// ---------------------------------------------------------------------------
// M2 Slice D1: authoritative flat WaterBody (Simulation/Marine)
// ---------------------------------------------------------------------------

bool WaterBodyValidConstruction()
{
    // Canonical M2-like configuration: sea level at world Y=0, seawater density.
    const auto atZero = DeepRun::Marine::WaterBody::Create(
        {.surfaceLevelY = 0.0F, .densityKgPerCubicMeter = 1025.0F});
    if (!atZero)
    {
        return false;
    }
    // The exact configuration must be retained without any correction.
    if (atZero->Config().surfaceLevelY != 0.0F || atZero->Config().densityKgPerCubicMeter != 1025.0F)
    {
        return false;
    }

    // An arbitrary shifted surface: the generic WaterBody must not assume sea level is world Y=0.
    const auto shifted = DeepRun::Marine::WaterBody::Create(
        {.surfaceLevelY = -37.5F, .densityKgPerCubicMeter = 998.2F});
    if (!shifted)
    {
        return false;
    }
    return shifted->Config().surfaceLevelY == -37.5F && shifted->Config().densityKgPerCubicMeter == 998.2F;
}

bool WaterBodySurfaceQueryAtZeroLevel()
{
    const auto created = DeepRun::Marine::WaterBody::Create(
        {.surfaceLevelY = 0.0F, .densityKgPerCubicMeter = 1025.0F});
    if (!created)
    {
        return false;
    }
    const DeepRun::Marine::WaterBody& water = *created;

    // y = 0: exactly on the surface.
    const auto onSurface = water.Sample({12.0F, 0.0F, -7.0F});
    if (!onSurface || onSurface->signedDepthMeters != 0.0F)
    {
        return false;
    }

    // y = -100: one hundred meters below the surface -> positive signed depth.
    const auto below = water.Sample({0.0F, -100.0F, 0.0F});
    if (!below || below->signedDepthMeters != 100.0F)
    {
        return false;
    }

    // y = +25: above the water -> negative signed depth.
    const auto above = water.Sample({0.0F, 25.0F, 0.0F});
    if (!above || above->signedDepthMeters != -25.0F)
    {
        return false;
    }

    // The normal is always +Y and every sample echoes the configured surface level.
    const DeepRun::Physics::PhysicsVector3 up{0.0F, 1.0F, 0.0F};
    return onSurface->surfaceNormal == up && below->surfaceNormal == up && above->surfaceNormal == up &&
           onSurface->surfaceLevelY == 0.0F && below->surfaceLevelY == 0.0F && above->surfaceLevelY == 0.0F;
}

bool WaterBodyShiftedSurface()
{
    const auto created = DeepRun::Marine::WaterBody::Create(
        {.surfaceLevelY = 50.0F, .densityKgPerCubicMeter = 1025.0F});
    if (!created)
    {
        return false;
    }

    // Depth must be measured from the configured surface, not from world Y=0: y=-50 is 100 m below a
    // surface at +50. This guards against hard-coding sea level into the generic WaterBody.
    const auto sample = created->Sample({0.0F, -50.0F, 0.0F});
    return sample && sample->signedDepthMeters == 100.0F && sample->surfaceLevelY == 50.0F;
}

bool WaterBodyXZIndependence()
{
    const auto created = DeepRun::Marine::WaterBody::Create(
        {.surfaceLevelY = 3.0F, .densityKgPerCubicMeter = 1025.0F});
    if (!created)
    {
        return false;
    }
    const DeepRun::Marine::WaterBody& water = *created;

    const auto reference = water.Sample({0.0F, -40.0F, 0.0F});
    if (!reference || reference->signedDepthMeters != 43.0F)
    {
        return false;
    }

    // X/Z must never affect the flat surface: same Y -> identical level, signed depth and normal.
    for (const auto position : {DeepRun::Physics::PhysicsVector3{1000.0F, -40.0F, 0.0F},
                                DeepRun::Physics::PhysicsVector3{-250.0F, -40.0F, 75.0F},
                                DeepRun::Physics::PhysicsVector3{0.0F, -40.0F, -9999.0F}})
    {
        const auto sample = water.Sample(position);
        if (!sample || sample->surfaceLevelY != reference->surfaceLevelY ||
            sample->signedDepthMeters != reference->signedDepthMeters ||
            sample->surfaceNormal != reference->surfaceNormal)
        {
            return false;
        }
    }
    return true;
}

bool WaterBodyInvalidConfigRejected()
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();

    const DeepRun::Marine::WaterBodyConfig invalidConfigs[] = {
        {.surfaceLevelY = nan, .densityKgPerCubicMeter = 1025.0F},
        {.surfaceLevelY = infinity, .densityKgPerCubicMeter = 1025.0F},
        {.surfaceLevelY = -infinity, .densityKgPerCubicMeter = 1025.0F},
        {.surfaceLevelY = 0.0F, .densityKgPerCubicMeter = 0.0F},
        {.surfaceLevelY = 0.0F, .densityKgPerCubicMeter = -1025.0F},
        {.surfaceLevelY = 0.0F, .densityKgPerCubicMeter = nan},
        {.surfaceLevelY = 0.0F, .densityKgPerCubicMeter = infinity}};

    for (const auto& config : invalidConfigs)
    {
        const auto result = DeepRun::Marine::WaterBody::Create(config);
        if (result.has_value() ||
            result.error().code != DeepRun::Marine::WaterBodyErrorCode::InvalidConfiguration ||
            result.error().message.empty())
        {
            return false;
        }
    }

    // Control: a valid configuration is still accepted after all rejections.
    const auto valid = DeepRun::Marine::WaterBody::Create(
        {.surfaceLevelY = 0.0F, .densityKgPerCubicMeter = 1025.0F});
    return valid.has_value();
}

bool WaterBodyRejectsNonFiniteQueryPosition()
{
    const auto created = DeepRun::Marine::WaterBody::Create(
        {.surfaceLevelY = 0.0F, .densityKgPerCubicMeter = 1025.0F});
    if (!created)
    {
        return false;
    }
    const DeepRun::Marine::WaterBody& water = *created;

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    for (const auto position : {DeepRun::Physics::PhysicsVector3{nan, 0.0F, 0.0F},
                                DeepRun::Physics::PhysicsVector3{0.0F, nan, 0.0F},
                                DeepRun::Physics::PhysicsVector3{0.0F, 0.0F, nan},
                                DeepRun::Physics::PhysicsVector3{infinity, -10.0F, infinity},
                                DeepRun::Physics::PhysicsVector3{-infinity, 25.0F, 0.0F}})
    {
        // Non-finite positions are recoverable errors instead of propagating NaN into gameplay state.
        const auto sample = water.Sample(position);
        if (sample.has_value() ||
            sample.error().code != DeepRun::Marine::WaterBodyErrorCode::InvalidQueryPosition)
        {
            return false;
        }
    }

    // Control: a finite position still samples successfully with the canonical sign convention.
    const auto ok = water.Sample({0.0F, -10.0F, 0.0F});
    return ok.has_value() && ok->signedDepthMeters == 10.0F;
}

// ---------------------------------------------------------------------------
// M2 Slice D2: world placement (asset pivot vs world position), water-surface viewport projection, and the
// generic renderer clear-rect validation. All pure — no Jolt, no D3D12 device, no GPU required.
// ---------------------------------------------------------------------------

namespace
{
using DeepRun::Marine::WaterBody;
using DeepRun::Render::OrthographicCamera;
using DeepRun::Render::RgbaColor;
using DeepRun::Render::ViewportRect;

constexpr float D2Tolerance = 1.0e-3F;

// The exact placement rule PhysicalPlayground uses (D2): X/Z from the asset bounds center, Y exclusively
// from WaterBody surface truth and the desired depth — never from the asset Y center.
DeepRun::Physics::PhysicsVector3 D2PlaceBody(
    const float surfaceLevelY,
    const float desiredDepthMeters,
    const ModelBounds& bounds)
{
    return DeepRun::Game::ComputeInitialBodyWorldCenter(
        surfaceLevelY, desiredDepthMeters, DeepRun::Game::BoundsCenter(bounds));
}

// The exact pivot composition PhysicalPlayground uses: bodyToWorld * T(-assetBoundsCenter). With the initial
// identity orientation this maps the model-space bounds center onto the world placement point.
ModelTransform D2ComposeBodyToAsset(
    const DeepRun::Physics::PhysicsVector3& bodyWorldCenter,
    const ModelBounds& bounds)
{
    const auto bodyToWorld =
        DeepRun::Game::BuildBodyToWorld(IdentityStateAt({bodyWorldCenter.x, bodyWorldCenter.y, bodyWorldCenter.z}));
    return Multiply(*bodyToWorld, DeepRun::Game::TranslationTransform(
                                      {-DeepRun::Game::BoundsCenter(bounds).x,
                                       -DeepRun::Game::BoundsCenter(bounds).y,
                                       -DeepRun::Game::BoundsCenter(bounds).z}));
}

// The canonical M2 gameplay camera (B2.1 contract): fixed 600 m horizontal span, orthographic side view,
// target at the initial body world center. Depth bounds are synthetic but finite; they only set near/far.
OrthographicCamera D2GameplayCamera(const float aspectRatio)
{
    const ModelBounds depthBounds{
        .minimum = {-50.0F, -10.0F, -4.0F},
        .maximum = {50.0F, 10.0F, 4.0F}};
    return *DeepRun::Render::BuildFixedWorldSideViewCamera(
        {1.0F, -100.0F, 0.0F}, aspectRatio, 600.0F, depthBounds);
}
} // namespace

bool D2OffCenterAssetPlacement()
{
    // Deliberately off-center asset bounds: center (20, 2, 1). The asset Y center (2) must NOT become the
    // body's world Y — only X/Z come from the asset space.
    const ModelBounds bounds = OffCenterTestBounds;
    constexpr float SurfaceLevelY = 0.0F;
    constexpr float DesiredDepthMeters = 100.0F;

    const auto water = WaterBody::Create(
        {.surfaceLevelY = SurfaceLevelY, .densityKgPerCubicMeter = 1025.0F});
    if (!water)
    {
        return false;
    }

    const DeepRun::Physics::PhysicsVector3 bodyCenter = D2PlaceBody(SurfaceLevelY, DesiredDepthMeters, bounds);
    if (std::abs(bodyCenter.x - 20.0F) > D2Tolerance || std::abs(bodyCenter.y + 100.0F) > D2Tolerance ||
        std::abs(bodyCenter.z - 1.0F) > D2Tolerance)
    {
        return false; // expected world center: (20, -100, 1)
    }

    // Pivot contract through the real composition: bodyToWorld * T(-asset center) must land the model-space
    // bounds center exactly on the world placement point.
    const ModelTransform composed = D2ComposeBodyToAsset(bodyCenter, bounds);
    const ModelVector3 transformedCenter = TransformPointBy(composed, DeepRun::Game::BoundsCenter(bounds));
    if (std::abs(transformedCenter.x - 20.0F) > D2Tolerance ||
        std::abs(transformedCenter.y + 100.0F) > D2Tolerance ||
        std::abs(transformedCenter.z - 1.0F) > D2Tolerance)
    {
        return false;
    }

    // The authoritative water body must report exactly the desired signed depth at that world center.
    const auto sample = water->Sample(bodyCenter);
    return sample && std::abs(sample->signedDepthMeters - DesiredDepthMeters) < D2Tolerance &&
           sample->surfaceLevelY == SurfaceLevelY;
}

bool D2ShiftedWaterSurfacePlacement()
{
    // The integration helper must not assume sea level at world Y=0: with a surface at +50 and a desired
    // depth of 100, the body center sits at Y = -50 — placement uses WaterBody truth, not "initialY = -100".
    const ModelBounds bounds = OffCenterTestBounds;
    constexpr float SurfaceLevelY = 50.0F;
    constexpr float DesiredDepthMeters = 100.0F;

    const auto water = WaterBody::Create(
        {.surfaceLevelY = SurfaceLevelY, .densityKgPerCubicMeter = 1025.0F});
    if (!water)
    {
        return false;
    }

    const DeepRun::Physics::PhysicsVector3 bodyCenter = D2PlaceBody(SurfaceLevelY, DesiredDepthMeters, bounds);
    if (std::abs(bodyCenter.x - 20.0F) > D2Tolerance || std::abs(bodyCenter.y + 50.0F) > D2Tolerance ||
        std::abs(bodyCenter.z - 1.0F) > D2Tolerance)
    {
        return false; // expected world center: (20, -50, 1)
    }

    const auto sample = water->Sample(bodyCenter);
    return sample && std::abs(sample->signedDepthMeters - DesiredDepthMeters) < D2Tolerance &&
           sample->surfaceLevelY == SurfaceLevelY;
}

bool D2SurfaceProjectionInsideViewport()
{
    // Gameplay camera: target Y = -100, horizontal span 600 m, aspect 16:9 -> vertical span 337.5 m. The
    // surface at world Y = 0 is 100 m above the camera center, so it must project inside the viewport and
    // above its middle: normalized Y from top ~= (1 - 100/168.75) * 0.5 ~= 0.2041.
    const OrthographicCamera camera = D2GameplayCamera(16.0F / 9.0F);
    if (std::abs(camera.width - 600.0F) > D2Tolerance || std::abs(camera.height - 337.5F) > D2Tolerance)
    {
        return false;
    }

    const auto surfaceY = DeepRun::Game::ProjectWorldSurfaceToViewportY(camera, 0.0F);
    if (!surfaceY || *surfaceY <= 0.0F || *surfaceY >= 1.0F)
    {
        return false; // must lie strictly inside the viewport and above the center (center is 0.5)
    }
    if (std::abs(*surfaceY - 0.2041667F) > 0.002F)
    {
        return false; // approximate normalized position, not an architectural pixel constant
    }

    const auto region = DeepRun::Game::UnderwaterRegionForSurface(camera, 0.0F);
    if (!region || !region->has_value())
    {
        return false;
    }
    const ViewportRect& rect = **region;
    return std::abs(rect.top - *surfaceY) < D2Tolerance && rect.left == 0.0F && rect.right == 1.0F &&
           rect.bottom == 1.0F;
}

bool D2SurfaceProjectionAspectRatioChange()
{
    // After an aspect change to 16:10 the width stays 600 m, the height becomes 375 m, and the surface still
    // derives from the same world Y=0 — only its normalized screen position changes (further from the top).
    const OrthographicCamera camera = D2GameplayCamera(16.0F / 10.0F);
    if (std::abs(camera.width - 600.0F) > D2Tolerance || std::abs(camera.height - 375.0F) > D2Tolerance)
    {
        return false;
    }

    const auto surfaceY = DeepRun::Game::ProjectWorldSurfaceToViewportY(camera, 0.0F);
    if (!surfaceY || *surfaceY <= 0.0F || *surfaceY >= 1.0F)
    {
        return false;
    }
    // (1 - 100/187.5) * 0.5 = 0.2333...: the same world Y, a different normalized position than at 16:9.
    if (std::abs(*surfaceY - 0.2333333F) > 0.002F)
    {
        return false;
    }

    const auto sixteenNine = DeepRun::Game::ProjectWorldSurfaceToViewportY(D2GameplayCamera(16.0F / 9.0F), 0.0F);
    return sixteenNine && *surfaceY > *sixteenNine; // taller viewport -> waterline lower in normalized terms
}

bool D2SurfaceProjectionEdgeCases()
{
    const OrthographicCamera camera = D2GameplayCamera(16.0F / 9.0F);

    // Surface far above the camera (Y = +500): the whole viewport is underwater.
    const auto above = DeepRun::Game::UnderwaterRegionForSurface(camera, 500.0F);
    if (!above || !above->has_value())
    {
        return false;
    }
    const ViewportRect& fullRect = **above;
    if (fullRect.left != 0.0F || fullRect.top != 0.0F || fullRect.right != 1.0F || fullRect.bottom != 1.0F)
    {
        return false;
    }

    // Surface inside the viewport (Y = 0): partial underwater region from the waterline to the bottom edge.
    const auto inside = DeepRun::Game::UnderwaterRegionForSurface(camera, 0.0F);
    if (!inside || !inside->has_value() || (**inside).top <= 0.0F || (**inside).top >= 1.0F)
    {
        return false;
    }

    // Surface below the viewport bottom (Y = -500): no visible underwater region, and no invalid rectangle.
    const auto below = DeepRun::Game::UnderwaterRegionForSurface(camera, -500.0F);
    if (!below || below->has_value())
    {
        return false;
    }

    // Non-finite input is rejected instead of producing a NaN rect.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    if (DeepRun::Game::ProjectWorldSurfaceToViewportY(camera, nan) ||
        DeepRun::Game::ProjectWorldSurfaceToViewportY(camera, infinity) ||
        DeepRun::Game::UnderwaterRegionForSurface(camera, nan) ||
        DeepRun::Game::UnderwaterRegionForSurface(camera, -infinity))
    {
        return false;
    }

    // A camera with non-finite projection data is rejected as well.
    OrthographicCamera broken = camera;
    broken.viewProjection.values[0] = nan;
    return !DeepRun::Game::ProjectWorldSurfaceToViewportY(broken, 0.0F) &&
           !DeepRun::Game::UnderwaterRegionForSurface(broken, 0.0F);
}

bool D2ClearRectValidation()
{
    using DeepRun::Render::ValidateViewportRect;

    // Valid normalized rect: accepted and clamped to itself (already inside [0, 1]).
    const auto valid = ValidateViewportRect({.left = 0.1F, .top = 0.25F, .right = 0.9F, .bottom = 1.0F});
    if (!valid || valid->left != 0.1F || valid->top != 0.25F || valid->right != 0.9F || valid->bottom != 1.0F)
    {
        return false;
    }

    // Out-of-range coordinates are clamped into the viewport (policy: clamp, then reject if empty).
    const auto clamped = ValidateViewportRect({.left = -0.5F, .top = -0.2F, .right = 1.4F, .bottom = 1.3F});
    if (!clamped || clamped->left != 0.0F || clamped->top != 0.0F || clamped->right != 1.0F ||
        clamped->bottom != 1.0F)
    {
        return false;
    }

    // Empty/inverted rects are rejected — the Game projection helper must clip before calling the renderer,
    // and a malformed rect must never reach the GPU.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    return !ValidateViewportRect({.left = 0.5F, .top = 0.0F, .right = 0.5F, .bottom = 1.0F}) && // zero width
           !ValidateViewportRect({.left = 0.9F, .top = 0.0F, .right = 0.1F, .bottom = 1.0F}) && // inverted X
           !ValidateViewportRect({.left = 0.0F, .top = 0.8F, .right = 1.0F, .bottom = 0.2F}) && // inverted Y
           !ValidateViewportRect({.left = nan, .top = 0.0F, .right = 1.0F, .bottom = 1.0F}) && // non-finite
           !ValidateViewportRect({.left = 0.0F, .top = infinity, .right = 1.0F, .bottom = 2.0F}) &&
           !ValidateViewportRect({.left = -5.0F, .top = 0.0F, .right = -1.0F, .bottom = 1.0F}); // fully outside -> empty after clamp
}

bool D2ClearColorValidation()
{
    using DeepRun::Render::ValidateRgbaColor;

    // Contract boundaries are accepted unchanged.
    const auto black = ValidateRgbaColor({0.0F, 0.0F, 0.0F, 0.0F});
    if (!black || black->r != 0.0F || black->a != 0.0F)
    {
        return false;
    }
    const auto white = ValidateRgbaColor({1.0F, 1.0F, 1.0F, 1.0F});
    if (!white || white->b != 1.0F)
    {
        return false;
    }

    // The current D2 presentation colors must pass without modification.
    const RgbaColor& expectedAbove = DeepRun::Game::M2AboveWaterBackgroundColor;
    const auto above = ValidateRgbaColor(expectedAbove);
    if (!above || above->r != expectedAbove.r || above->g != expectedAbove.g ||
        above->b != expectedAbove.b || above->a != expectedAbove.a)
    {
        return false;
    }
    const auto below = ValidateRgbaColor(DeepRun::Game::M2UnderwaterBackgroundColor);
    if (!below)
    {
        return false;
    }

    // Out-of-range and non-finite components are rejected — never silently clamped.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    return !ValidateRgbaColor({-0.1F, 0.5F, 0.5F, 1.0F}) &&   // negative r
           !ValidateRgbaColor({0.5F, 1.2F, 0.5F, 1.0F}) &&    // g > 1
           !ValidateRgbaColor({0.5F, 0.5F, 0.5F, -1.0F}) &&   // negative a
           !ValidateRgbaColor({nan, 0.5F, 0.5F, 1.0F}) &&     // NaN r
           !ValidateRgbaColor({0.5F, infinity, 0.5F, 1.0F}) && // Inf g
           !ValidateRgbaColor({-infinity, 0.5F, 0.5F, 1.0F}); // -Inf r
}

bool D2PixelRectConversion()
{
    using DeepRun::Render::ToPixelRect;

    // Focused conversion case: normalized {0, 0.2, 1, 1} on a 1280x720 target -> approximately
    // pixel rect {0, 144, 1280, 720} (top-left origin, right/bottom exclusive).
    const auto quarter = ToPixelRect({.left = 0.0F, .top = 0.2F, .right = 1.0F, .bottom = 1.0F}, 1280U,
                                     720U);
    if (!quarter)
    {
        return false;
    }
    // Signed arithmetic: an unsigned `expected - 1` would wrap around for expected == 0.
    const auto nearPixel = [](const std::uint32_t value, const std::uint32_t expected) noexcept {
        return static_cast<std::int64_t>(value) >= static_cast<std::int64_t>(expected) - 1 &&
               static_cast<std::int64_t>(value) <= static_cast<std::int64_t>(expected) + 1;
    };
    if (!nearPixel(quarter->left, 0U) || !nearPixel(quarter->top, 144U) ||
        !nearPixel(quarter->right, 1280U) || !nearPixel(quarter->bottom, 720U))
    {
        return false;
    }

    // The full viewport maps to the exact target extents.
    const auto full = ToPixelRect({.left = 0.0F, .top = 0.0F, .right = 1.0F, .bottom = 1.0F}, 1280U,
                                  720U);
    if (!full || full->left != 0U || full->top != 0U || full->right != 1280U || full->bottom != 720U)
    {
        return false;
    }

    // Malformed input is rejected: zero-size target and non-finite rect.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    return !ToPixelRect({.left = 0.0F, .top = 0.0F, .right = 1.0F, .bottom = 1.0F}, 0U, 720U) &&
           !ToPixelRect({.left = nan, .top = 0.0F, .right = 1.0F, .bottom = 1.0F}, 1280U, 720U);
}

bool D2PresentationColorsAreDistinct()
{
    // The two M2 presentation colors must be visibly distinct (obvious above/underwater difference) and
    // opaque — no alpha blending is part of the D2 contract.
    const RgbaColor& above = DeepRun::Game::M2AboveWaterBackgroundColor;
    const RgbaColor& below = DeepRun::Game::M2UnderwaterBackgroundColor;
    if (!above.IsFinite() || !below.IsFinite() || above.a != 1.0F || below.a != 1.0F)
    {
        return false;
    }
    const float channelDifference = std::abs(above.r - below.r) + std::abs(above.g - below.g) +
                                    std::abs(above.b - below.b);
    return channelDifference > 0.25F; // clearly different, without asserting final art direction
}

// ---------------------------------------------------------------------------
// M2 Slice C2: architecture boundary scans
// ---------------------------------------------------------------------------

bool ScanSourceDirectoryForForbiddenPatterns(
    const std::filesystem::path& root,
    const std::vector<std::string_view>& forbidden)
{
    if (!std::filesystem::exists(root))
    {
        return false;
    }
    for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(root))
    {
        if (!entry.is_regular_file())
        {
            continue;
        }
        const std::filesystem::path extension = entry.path().extension();
        if (extension != ".h" && extension != ".cpp")
        {
            continue;
        }

        std::ifstream input(entry.path(), std::ios::binary);
        std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        std::ranges::transform(contents, contents.begin(), [](const unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        for (const std::string_view pattern : forbidden)
        {
            if (contents.find(pattern) != std::string::npos)
            {
                return false;
            }
        }
    }
    return true;
}

bool GameCodeHasNoJoltDependency()
{
    // Gameplay code must not include JPH headers or use JPH types (ADR-0002 / ADR-0007).
    const std::filesystem::path gameRoot = std::filesystem::path(DEEPRUN_SOURCE_ROOT) / "Game";
    return ScanSourceDirectoryForForbiddenPatterns(gameRoot, {"<jolt/", "jph::"});
}

bool EngineRenderHasNoPhysicsOrJoltDependency()
{
    // The renderer must not know about Jolt or physics handles (ADR-0007).
    const std::filesystem::path renderRoot = std::filesystem::path(DEEPRUN_SOURCE_ROOT) / "Engine" / "Render";
    return ScanSourceDirectoryForForbiddenPatterns(renderRoot, {"<jolt/", "jph::", "physicsbodyhandle"});
}

bool PhysicsWorldHasNoGpuModelKnowledge()
{
    // PhysicsWorld must not know about GPU models (ADR-0007).
    const std::filesystem::path physicsRoot = std::filesystem::path(DEEPRUN_SOURCE_ROOT) / "Engine" / "Physics";
    return ScanSourceDirectoryForForbiddenPatterns(physicsRoot, {"gpumodel", "d3d12"});
}

bool SimulationMarineHasNoPhysicsOrRenderDependency()
{
    // Marine simulation must stay free of Jolt, D3D12 and renderer knowledge (M2 Slice D1): WaterBody is an
    // authoritative environment primitive, not a collision body or a render feature.
    const std::filesystem::path marineRoot =
        std::filesystem::path(DEEPRUN_SOURCE_ROOT) / "Simulation" / "Marine";
    return ScanSourceDirectoryForForbiddenPatterns(
        marineRoot,
        {"<jolt/", "jph::", "d3d12", "directxmath", "modelvector3", "physicsbodyhandle",
         "gpumodelhandle", "render/"});
}

bool EngineHasNoMarineKnowledge()
{
    // The generic engine must not become aware of marine simulation (M2 Slice D1).
    const std::filesystem::path engineRoot = std::filesystem::path(DEEPRUN_SOURCE_ROOT) / "Engine";
    return ScanSourceDirectoryForForbiddenPatterns(engineRoot, {"waterbody", "marine"});
}

bool EngineRenderHasNoWaterSemantics()
{
    // The renderer stays generic (M2 Slice D2): it knows rectangles and colors, never water. Game owns the
    // presentation semantics; the clear-rect API must not grow marine vocabulary.
    const std::filesystem::path renderRoot = std::filesystem::path(DEEPRUN_SOURCE_ROOT) / "Engine" / "Render";
    return ScanSourceDirectoryForForbiddenPatterns(
        renderRoot, {"water", "ocean", "sealevel", "submarinedepth"});
}
} // namespace

int main(const int argumentCount, const char* const* arguments)
{
    if (argumentCount == 3 && std::string_view(arguments[1]) == "--asset-root")
    {
        testAssetRoot = arguments[2];
    }
    else
    {
        testAssetRoot = "Assets";
    }

    const std::vector<Test> tests{
        {"Core startup/shutdown", CoreStartupShutdown},
        {"Frame lifecycle", FrameLifecycle},
        {"Frame rebase preserves state", FrameRebasePreservesState},
        {"Fixed-step scheduling", FixedStepScheduling},
        {"Headless engine lifecycle", EngineHeadlessLifecycle},
        {"Scene entity lifecycle", SceneEntityLifecycle},
        {"Resource identity and cache", ResourceIdentityAndCache},
        {"Asset path normalization", AssetPathNormalization},
        {"Canonical C0 model load", CanonicalModelLoads},
        {"Canonical C0 indexed geometry", CanonicalModelHasIndexedGeometry},
        {"Canonical C0 model bounds", CanonicalModelHasFiniteBounds},
        {"Canonical C0 material transport", CanonicalModelPreservesMaterial},
        {"Canonical C0 node structure", CanonicalModelPreservesStructure},
        {"Canonical indexed GPU layout", CanonicalIndexedGpuLayout},
        {"Invalid indexed GPU layout", InvalidIndexedGpuLayoutIsRejected},
        {"Default GPU model handle", DefaultGpuModelHandleIsInvalid},
        {"Generic Engine Core dependency boundary", GenericEngineCoreHasNoSubmarineDependency},
        {"Auto-fit side-view camera contract", AutoFitSideViewCameraContract},
        {"Fixed-world camera span contract", FixedWorldCameraSpanContract},
        {"Fixed-world camera model-size independence", FixedWorldCameraIgnoresModelSize},
        {"Fixed-world camera screen scale", FixedWorldCameraScreenScale},
        {"Fixed-world camera translation", FixedWorldCameraTranslationContract},
        {"Fixed-world camera input validation", FixedWorldCameraRejectsInvalidInput},
        {"Model and normal transform contract", ModelAndNormalTransformContract},
        {"Model draw preparation contract", ModelDrawPreparationContract},
        {"Invalid GPU model draw", InvalidGpuModelDrawIsRejected},
        {"Model identity and cache", ModelIdentityAndCache},
        {"Missing model asset", MissingModelAsset},
        {"Invalid model data", InvalidModelDataIsRejected},
        {"Material defaults and references", MaterialDefaultsAndInvalidReference},
        {"Configuration loading", ConfigurationLoading},
        {"Input state transitions", InputStateTransitions},
        {"Deterministic random", DeterministicRandom},
        {"Jolt initialization", JoltInitialization},
        {"Rigid-body gravity", RigidBodySimulation},
        {"Physics handle semantics", PhysicsHandleSemantics},
        {"Dynamic box input validation", DynamicBoxInputValidation},
        {"Physics pose round-trip", PhysicsPoseRoundTrip},
        {"Physics gravity fall", PhysicsGravityFalls},
        {"Physics gravity disabled stays still", PhysicsGravityDisabledStaysStill},
        {"No hidden linear drag", NoHiddenLinearDrag},
        {"Angular velocity state", AngularVelocityState},
        {"Audio abstraction", AudioBoundary},
        // M2 Slice C2: physics-to-render synchronization math and integration.
        {"Physics render sync pivot contract", PhysicsRenderSyncPivotContract},
        {"Physics render sync translation applied once", PhysicsRenderSyncTranslationAppliedOnce},
        {"Physics render sync Z rotation convention", PhysicsRenderSyncZRotationConvention},
        {"Physics render sync quaternion sign equivalence", PhysicsRenderSyncQuaternionSignEquivalence},
        {"Physics render sync propeller stays attached", PhysicsRenderSyncPropellerStaysAttached},
        {"Physics render sync transformed bounds", PhysicsRenderSyncTransformedBounds},
        {"Physics render sync rejects non-finite input", PhysicsRenderSyncRejectsNonFiniteInput},
        {"Physics integration submarine body falls through pivot", PhysicsIntegrationSubmarineBodyFallsThroughPivot},
        // M2 Slice C2.1: 2.5D rigid-body DOF contract and corrected world-bounds composition.
        {"Default body keeps all six DOFs", DefaultBodyKeepsAllSixDOFs},
        {"Planar body stays in gameplay plane", PlanarBodyStaysInGameplayPlane},
        {"Planar body rejects locked-axis initial velocity", PlanarBodyRejectsLockedAxisInitialVelocity},
        {"World bounds use model-to-world composition", WorldBoundsUseModelToWorldComposition},
        // M2 Slice D1: authoritative flat WaterBody (Simulation/Marine).
        {"Water body valid construction", WaterBodyValidConstruction},
        {"Water body surface query at zero level", WaterBodySurfaceQueryAtZeroLevel},
        {"Water body shifted surface", WaterBodyShiftedSurface},
        {"Water body X/Z independence", WaterBodyXZIndependence},
        {"Water body invalid config rejected", WaterBodyInvalidConfigRejected},
        {"Water body rejects non-finite query position", WaterBodyRejectsNonFiniteQueryPosition},
        // M2 Slice D2: world placement, water-surface viewport projection, and generic clear-rect validation.
        {"D2 off-center asset world placement", D2OffCenterAssetPlacement},
        {"D2 shifted water surface placement", D2ShiftedWaterSurfacePlacement},
        {"D2 surface projection inside viewport", D2SurfaceProjectionInsideViewport},
        {"D2 surface projection aspect ratio change", D2SurfaceProjectionAspectRatioChange},
        {"D2 surface projection edge cases", D2SurfaceProjectionEdgeCases},
        {"D2 clear rect validation policy", D2ClearRectValidation},
        {"D2 clear color validation contract", D2ClearColorValidation},
        {"D2 pixel rect conversion", D2PixelRectConversion},
        {"D2 presentation colors distinct and opaque", D2PresentationColorsAreDistinct},
        // M2 Slice E1: generic force-at-world-position PhysicsWorld API (headless, public API only).
        {"E1 centered force produces translation", ForceCenteredProducesTranslation},
        {"E1 off-center force produces torque", ForceOffCenterProducesTorque},
        {"E1 opposite point flips torque sign", ForceOppositePointFlipsTorqueSign},
        {"E1 symmetric points cancel torque", ForceSymmetricPointsCancelTorque},
        {"E1 multiple calls accumulate", ForceMultipleCallsAccumulate},
        {"E1 force is not persistent", ForceIsNotPersistent},
        {"E1 planar DOF preservation under force", ForcePlanarDOFPreservation},
        {"E1 invalid input and zero-force no-op", ForceInvalidInputAndZeroNoOp},
        {"E1 non-zero force activates sleeping body", ForceActivatesSleepingBody},
        // M2 Slice C2: architecture boundary scans.
        {"Game code has no Jolt dependency", GameCodeHasNoJoltDependency},
        {"Engine render has no physics or Jolt dependency", EngineRenderHasNoPhysicsOrJoltDependency},
        {"Physics world has no GPU model knowledge", PhysicsWorldHasNoGpuModelKnowledge},
        // M2 Slice D1: architecture boundary scans.
        {"Simulation marine has no physics or render dependency",
         SimulationMarineHasNoPhysicsOrRenderDependency},
        {"Engine has no marine knowledge", EngineHasNoMarineKnowledge},
        // M2 Slice D2: renderer stays generic — no water semantics in Engine/Render.
        {"Engine render has no water semantics", EngineRenderHasNoWaterSemantics},
    };

    int failed = 0;
    for (const auto& [name, test] : tests)
    {
        try
        {
            if (test())
            {
                std::cout << "[PASS] " << name << '\n';
            }
            else
            {
                std::cerr << "[FAIL] " << name << '\n';
                ++failed;
            }
        }
        catch (const std::exception& exception)
        {
            std::cerr << "[FAIL] " << name << ": " << exception.what() << '\n';
            ++failed;
        }
    }

    std::cout << tests.size() - static_cast<std::size_t>(failed) << '/' << tests.size() << " tests passed\n";
    return failed == 0 ? 0 : 1;
}
