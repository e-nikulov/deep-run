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
#include "Engine/Render/D3D12Renderer.h"
#include "Engine/Render/IndexedGeometry.h"
#include "Engine/Render/ModelDraw.h"
#include "Engine/Scene/Scene.h"

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
}

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
        {"Audio abstraction", AudioBoundary},
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
