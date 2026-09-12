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
#include "Engine/Diagnostics/FrameStatistics.h"
#include "Engine/Input/HapticMixer.h"
#include "Engine/Input/InputState.h"
#include "Engine/Input/InputSystem.h"
#include "Engine/Input/Windows/WindowsGamingInputGamepad.h"
#include "Engine/Physics/PhysicsWorld.h"
#include "Engine/Render/Camera.h"
#include "Engine/Render/ClearRect.h"
#include "Engine/Render/D3D12Renderer.h"
#include "Engine/Render/DisplayOutput.h"
#include "Engine/Render/GerstnerSurface.h"
#include "Engine/Render/IndexedGeometry.h"
#include "Engine/Render/ModelDraw.h"
#include "Engine/Render/DepthLighting.h"
#include "Engine/Render/SuspendedParticles.h"
#include "Engine/Render/ViewPathFog.h"
#include "Engine/Scene/Scene.h"
#include "Game/Environment/EnvironmentSection.h"
#include "Game/Environment/UnderwaterFaunaField.h"
#include "Game/Environment/UnderwaterFloraField.h"
#include "Game/Environment/UnderwaterIceField.h"
#include "Game/Haptics/HapticFeedbackSystem.h"
#include "Game/PhysicsRenderSync.h"
#include "Game/PropulsionPresentation.h"
#include "Game/Submarine/ProductionAnteyAsset.h"
#include "Game/Submarine/ProductionAnteyLodPolicy.h"
#include "Game/SurfaceFloatModel.h"
#include "Game/WaterPresentation.h"
#include "Game/Submarine/VesselCommandState.h"
#include "Simulation/Marine/BuoyancySystem.h"
#include "Simulation/Marine/ControlSurfaceSystem.h"
#include "Simulation/Marine/HydroDragSystem.h"
#include "Simulation/Marine/PropulsionSystem.h"
#include "Simulation/Marine/WaterBody.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <cmath>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <set>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
using Test = std::pair<std::string_view, std::function<bool()>>;
constexpr std::string_view CanonicalModelPath = "submarines/prototype/submarine_prototype.glb";
constexpr std::string_view AnteyModelPath = "submarines/Antey/Antey.glb";
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

[[nodiscard]] std::string ReadFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

[[nodiscard]] int RunAnteyStaging(
    const std::filesystem::path& sourceDirectory,
    const std::filesystem::path& destinationDirectory)
{
    const std::filesystem::path script = std::filesystem::path(DEEPRUN_SOURCE_ROOT) / "cmake/StageAnteyRuntime.cmake";
    const std::string command = std::string{"cmake -DDEEPRUN_ANTEY_SOURCE_DIR=\""} +
                                sourceDirectory.string() + "\" -DDEEPRUN_ANTEY_DESTINATION_DIR=\"" +
                                destinationDirectory.string() + "\" -P \"" + script.string() + "\"";
    return std::system(command.c_str());
}

[[nodiscard]] std::set<std::string> RelativeFiles(const std::filesystem::path& root)
{
    std::set<std::string> result;
    for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(root))
    {
        if (entry.is_regular_file())
        {
            result.insert(std::filesystem::relative(entry.path(), root).generic_string());
        }
    }
    return result;
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

bool IG1AStagesValidatedProductionPackageDeterministically()
{
    const std::filesystem::path sourceDirectory =
        std::filesystem::path(DEEPRUN_SOURCE_ROOT) / "Content/submarines/Antey";
    TemporaryDirectory temporary;
    const std::filesystem::path destinationDirectory = temporary.Path() / "Assets/submarines/Antey";
    if (RunAnteyStaging(sourceDirectory, destinationDirectory) != 0)
    {
        return false;
    }

    const std::set<std::string> expectedFiles{"Antey.asset.json", "Antey.authoring.json", "Antey.glb"};
    if (RelativeFiles(destinationDirectory) != expectedFiles ||
        std::filesystem::exists(destinationDirectory / "Antey_Source.blend"))
    {
        return false;
    }

    for (const std::string& file : expectedFiles)
    {
        if (ReadFile(sourceDirectory / file) != ReadFile(destinationDirectory / file))
        {
            return false;
        }
    }

    const std::string firstGlb = ReadFile(destinationDirectory / "Antey.glb");
    const std::string firstAssetMetadata = ReadFile(destinationDirectory / "Antey.asset.json");
    const std::string firstAuthoringMetadata = ReadFile(destinationDirectory / "Antey.authoring.json");
    if (RunAnteyStaging(sourceDirectory, destinationDirectory) != 0)
    {
        return false;
    }
    return firstGlb == ReadFile(destinationDirectory / "Antey.glb") &&
           firstAssetMetadata == ReadFile(destinationDirectory / "Antey.asset.json") &&
           firstAuthoringMetadata == ReadFile(destinationDirectory / "Antey.authoring.json");
}

bool IG1AStagingRejectsMissingOrLegacyProductionInput()
{
    TemporaryDirectory temporary;
    const std::filesystem::path missingSource = temporary.Path() / "missing";
    if (RunAnteyStaging(missingSource, temporary.Path() / "missing-output") == 0)
    {
        return false;
    }

    const std::filesystem::path sourceDirectory =
        std::filesystem::path(DEEPRUN_SOURCE_ROOT) / "Content/submarines/Antey";
    const std::filesystem::path legacySource = temporary.Path() / "legacy";
    std::filesystem::create_directories(legacySource);
    for (const std::string_view file : {"Antey.glb", "Antey.asset.json", "Antey.authoring.json"})
    {
        std::filesystem::copy_file(sourceDirectory / file, legacySource / file);
    }
    std::string authoring = ReadFile(legacySource / "Antey.authoring.json");
    authoring.replace(authoring.find("SM_Propeller_Port"), std::string_view("SM_Propeller_Port").size(), "HP_Antey_Legacy");
    WriteFile(legacySource / "Antey.authoring.json", authoring);
    return RunAnteyStaging(legacySource, temporary.Path() / "legacy-output") != 0;
}

bool IG1AStagedProductionDefinitionLoadsHeadlessly()
{
    DeepRun::Assets::AssetManager assets(testAssetRoot);
    const auto definition = DeepRun::Game::Submarine::LoadProductionAnteyAssetDefinition(assets);
    if (!definition)
    {
        std::cerr << "[IG1-A] " << definition.error() << '\n';
        return false;
    }
    if (definition->assetFamilyId != "submarine.antey" ||
        definition->metadataAssetId.Value() != "submarines/Antey/Antey.asset.json" || definition->propellers.size() != 2U ||
        definition->retractableSailDevices.size() != 9U || definition->torpedoLaunchAnchors.size() != 6U ||
        definition->p700LaunchAnchors.size() != 24U ||
        definition->compartments.size() != 10U || definition->collisionProxies.size() != 1U ||
        definition->collisionProxies.front().semanticId != "collision.primary" ||
        definition->buoyancyProxy.semanticId != "buoyancy.primary")
    {
        return false;
    }

    // Detailed geometry and source-first semantic checks live in the focused
    // IG1-B / IG1-B.1 tests below; this gate only proves staged loading.
    return true;
}

bool IG1A1AuthoringTransformConversionPreservesAffineInvariant()
{
    using DeepRun::Assets::ModelTransform;
    using DeepRun::Assets::ModelVector3;
    const std::array<float, 16> sourceRowMajor{
        0.0F, -1.0F, 0.0F, 3.0F,
        1.0F, 0.0F, 0.0F, -4.0F,
        0.0F, 0.0F, 1.0F, 5.0F,
        0.0F, 0.0F, 0.0F, 1.0F};
    const auto applyRowMajor = [](const std::array<float, 16>& matrix, const ModelVector3 point)
    {
        return ModelVector3{
            .x = matrix[0] * point.x + matrix[1] * point.y + matrix[2] * point.z + matrix[3],
            .y = matrix[4] * point.x + matrix[5] * point.y + matrix[6] * point.z + matrix[7],
            .z = matrix[8] * point.x + matrix[9] * point.y + matrix[10] * point.z + matrix[11]};
    };
    const auto applyColumnMajor = [](const ModelTransform& matrix, const ModelVector3 point)
    {
        return ModelVector3{
            .x = matrix.values[0] * point.x + matrix.values[4] * point.y + matrix.values[8] * point.z + matrix.values[12],
            .y = matrix.values[1] * point.x + matrix.values[5] * point.y + matrix.values[9] * point.z + matrix.values[13],
            .z = matrix.values[2] * point.x + matrix.values[6] * point.y + matrix.values[10] * point.z + matrix.values[14]};
    };
    const ModelVector3 sourcePoint{.x = 2.0F, .y = -3.0F, .z = 4.0F};
    const ModelVector3 expected = DeepRun::Game::Submarine::ConvertAnteyAuthoringVector(
        applyRowMajor(sourceRowMajor, sourcePoint));
    const ModelVector3 actual = applyColumnMajor(
        DeepRun::Game::Submarine::ConvertAnteyAuthoringTransform(sourceRowMajor),
        DeepRun::Game::Submarine::ConvertAnteyAuthoringVector(sourcePoint));
    const auto runtimeQuaternion = DeepRun::Game::Submarine::ConvertAnteyAuthoringQuaternionWxyz(
        {0.8660254F, 0.0F, 0.5F, 0.0F});
    const auto rotateQuaternion = [](const std::array<float, 4>& quaternion, const ModelVector3 vector)
    {
        const ModelVector3 q{.x = quaternion[1], .y = quaternion[2], .z = quaternion[3]};
        const ModelVector3 cross{
            .x = q.y * vector.z - q.z * vector.y,
            .y = q.z * vector.x - q.x * vector.z,
            .z = q.x * vector.y - q.y * vector.x};
        const ModelVector3 doubleCross{
            .x = q.y * cross.z - q.z * cross.y,
            .y = q.z * cross.x - q.x * cross.z,
            .z = q.x * cross.y - q.y * cross.x};
        return ModelVector3{
            .x = vector.x + 2.0F * (quaternion[0] * cross.x + doubleCross.x),
            .y = vector.y + 2.0F * (quaternion[0] * cross.y + doubleCross.y),
            .z = vector.z + 2.0F * (quaternion[0] * cross.z + doubleCross.z)};
    };
    const ModelVector3 quaternionSourceVector{.x = 1.0F, .y = 2.0F, .z = -3.0F};
    const ModelVector3 quaternionExpected = DeepRun::Game::Submarine::ConvertAnteyAuthoringVector(
        rotateQuaternion({0.8660254F, 0.0F, 0.5F, 0.0F}, quaternionSourceVector));
    const ModelVector3 quaternionActual = rotateQuaternion(
        runtimeQuaternion, DeepRun::Game::Submarine::ConvertAnteyAuthoringVector(quaternionSourceVector));
    const float quaternionLength = runtimeQuaternion[0] * runtimeQuaternion[0] + runtimeQuaternion[1] * runtimeQuaternion[1] +
                                   runtimeQuaternion[2] * runtimeQuaternion[2] + runtimeQuaternion[3] * runtimeQuaternion[3];
    return std::abs(actual.x - expected.x) < 1.0e-5F && std::abs(actual.y - expected.y) < 1.0e-5F &&
           std::abs(actual.z - expected.z) < 1.0e-5F && std::abs(quaternionLength - 1.0F) < 1.0e-4F &&
           std::abs(quaternionActual.x - quaternionExpected.x) < 1.0e-4F &&
           std::abs(quaternionActual.y - quaternionExpected.y) < 1.0e-4F &&
           std::abs(quaternionActual.z - quaternionExpected.z) < 1.0e-4F;
}

bool IG1BProductionVisualSelectionAndRuntimeGeometry()
{
    DeepRun::Assets::AssetManager assets(testAssetRoot);
    const auto definition = DeepRun::Game::Submarine::LoadProductionAnteyAssetDefinition(assets);
    if (!definition)
    {
        return false;
    }
    const auto lod0 = DeepRun::Game::Submarine::SelectProductionAnteyRenderAsset(
        *definition, DeepRun::Game::Submarine::ProductionRenderLodLevel::Lod0);
    if (!lod0 || definition->assetFamilyId != "submarine.antey" ||
        lod0->selected != DeepRun::Game::Submarine::ProductionRenderLodLevel::Lod0 || lod0->usedFallback ||
        lod0->assetId.Value() != AnteyModelPath)
    {
        return false;
    }
    const auto production = assets.LoadModel(std::filesystem::path(lod0->assetId.Value()));
    const auto prototype = assets.LoadModel(CanonicalModelPath);
    if (!production || !prototype || production->Get()->id.Value() != AnteyModelPath || production->Get()->nodes.empty() ||
        production->Get()->primitives.empty() || production->Get()->materials.size() != 2U)
    {
        return false;
    }
    const auto& bounds = production->Get()->bounds;
    const float length = bounds.maximum.x - bounds.minimum.x;
    const float height = bounds.maximum.y - bounds.minimum.y;
    const float beam = bounds.maximum.z - bounds.minimum.z;
    std::uint64_t vertices = 0U;
    std::uint64_t indices = 0U;
    for (const auto& primitive : production->Get()->primitives)
    {
        if (primitive.vertices.empty() || primitive.indices.empty() || !primitive.hasNormals)
        {
            return false;
        }
        vertices += primitive.vertices.size();
        indices += primitive.indices.size();
    }
    std::cout << "[IG1-B evidence] production nodes=" << production->Get()->nodes.size() << ", primitives="
              << production->Get()->primitives.size() << ", vertices=" << vertices << ", indices=" << indices
              << ", triangles=" << indices / 3U << ", materials=" << production->Get()->materials.size()
              << ", bounds x[" << bounds.minimum.x << ',' << bounds.maximum.x << "] y[" << bounds.minimum.y
              << ',' << bounds.maximum.y << "] z[" << bounds.minimum.z << ',' << bounds.maximum.z << "]\n";
    return std::isfinite(length) && std::isfinite(height) && std::isfinite(beam) && length >= 150.0F && length <= 158.0F &&
           height > 0.0F && beam > 0.0F && length > height && length > beam &&
           length > prototype->Get()->bounds.maximum.x - prototype->Get()->bounds.minimum.x &&
           vertices > 0U && indices > 0U && indices % 3U == 0U;
}

bool IG1BNormalPlaygroundVisualIsIsolatedFromM2PhysicsBridge()
{
    const std::string source = ReadFile(std::filesystem::path(DEEPRUN_SOURCE_ROOT) / "Game/PhysicalPlayground.cpp");
    const std::size_t productionSelection = source.find("SelectProductionAnteyRenderAsset(");
    const std::size_t productionLoad = source.find("assets.LoadModel(std::filesystem::path(productionLodSelection->assetId.Value()))");
    const std::size_t prototypeBridge = source.find("M2PhysicsProxyModelPath");
    const std::size_t productionCollision = source.find("collisionProxy.halfExtents");
    const std::size_t bodyHalfExtents = source.find("bodyInfo.halfExtents = collisionHalfExtents");
    return productionSelection != std::string::npos && productionLoad != std::string::npos &&
           source.find("legacyLod0") == std::string::npos && source.find("productionLod0") == std::string::npos &&
           prototypeBridge == std::string::npos && productionCollision != std::string::npos &&
           bodyHalfExtents != std::string::npos && source.find("assets.LoadModel(M2PhysicsProxyModelPath)") == std::string::npos &&
           source.find("SubmarineModelPath") == std::string::npos &&
           source.find("Render::PrepareModelDraws(*modelAsset_, modelToWorld,") != std::string::npos;
}

bool IG1CProductionProxyContractsAreLoadedAndIndependentFromVisualBounds()
{
    DeepRun::Assets::AssetManager assets(testAssetRoot);
    const auto definition = DeepRun::Game::Submarine::LoadProductionAnteyAssetDefinition(assets);
    if (!definition || definition->collisionProxies.size() != 1U ||
        definition->collisionProxies.front().shape != DeepRun::Game::Submarine::ProductionProxyShape::Box ||
        definition->buoyancyProxy.shape != DeepRun::Game::Submarine::ProductionProxyShape::Box)
    {
        return false;
    }

    const auto& collision = definition->collisionProxies.front();
    const auto& buoyancy = definition->buoyancyProxy;
    const auto nearlyEqual = [](float actual, float expected) { return std::abs(actual - expected) <= 1.0e-4F; };
    const bool collisionContract = nearlyEqual(collision.halfExtents.x, 77.0F) &&
        nearlyEqual(collision.halfExtents.y, 5.0F) && nearlyEqual(collision.halfExtents.z, 8.9F) &&
        nearlyEqual(collision.localCenter.x, 0.0F) && nearlyEqual(collision.localCenter.y, 0.0F) &&
        nearlyEqual(collision.localCenter.z, 0.0F);
    const bool buoyancyContract = nearlyEqual(buoyancy.halfExtents.x, 72.5F) &&
        nearlyEqual(buoyancy.halfExtents.y, 3.75F) && nearlyEqual(buoyancy.halfExtents.z, 8.0F) &&
        nearlyEqual(buoyancy.centerOfBuoyancy.x, 0.0F) && nearlyEqual(buoyancy.centerOfBuoyancy.y, 0.0F) &&
        nearlyEqual(buoyancy.centerOfBuoyancy.z, 0.0F);
    const std::string source = ReadFile(std::filesystem::path(DEEPRUN_SOURCE_ROOT) / "Game/PhysicalPlayground.cpp");
    const bool noVisualPhysicsBridge = source.find("physicsProxyModel") == std::string::npos &&
        source.find("physicsBounds") == std::string::npos && source.find("M2PhysicsProxyModelPath") == std::string::npos &&
        source.find("BoundsCenter(visualBounds)") == std::string::npos &&
        source.find("bodyInfo.halfExtents = collisionHalfExtents") != std::string::npos;
    return collisionContract && buoyancyContract && noVisualPhysicsBridge;
}

bool IG1CProductionBuoyancyLayoutIsBoundedAndGameTuned()
{
    DeepRun::Assets::AssetManager assets(testAssetRoot);
    const auto definition = DeepRun::Game::Submarine::LoadProductionAnteyAssetDefinition(assets);
    if (!definition || definition->buoyancyProxy.halfExtents.x <= 0.0F ||
        definition->buoyancyProxy.halfExtents.y <= 0.0F || definition->buoyancyProxy.halfExtents.z <= 0.0F)
    {
        return false;
    }

    constexpr std::array<float, 4> Fractions{0.5F, 1.0F / 6.0F, -1.0F / 6.0F, -0.5F};
    std::array<float, 4> longitudinalPositions{};
    for (std::size_t index = 0; index < Fractions.size(); ++index)
    {
        longitudinalPositions[index] = definition->buoyancyProxy.localCenter.x +
            Fractions[index] * definition->buoyancyProxy.halfExtents.x;
        if (std::abs(longitudinalPositions[index] - definition->buoyancyProxy.localCenter.x) >
                definition->buoyancyProxy.halfExtents.x + 1.0e-4F ||
            !std::isfinite(longitudinalPositions[index]))
        {
            return false;
        }
    }
    const bool symmetric = std::abs(longitudinalPositions[0] + longitudinalPositions[3] -
                                    2.0F * definition->buoyancyProxy.localCenter.x) <= 1.0e-4F &&
        std::abs(longitudinalPositions[1] + longitudinalPositions[2] -
                 2.0F * definition->buoyancyProxy.localCenter.x) <= 1.0e-4F;
    const std::string source = ReadFile(std::filesystem::path(DEEPRUN_SOURCE_ROOT) / "Game/PhysicalPlayground.cpp");
    const bool productionSpatialPolicy = source.find("productionBuoyancy.halfExtents.x") != std::string::npos &&
        source.find("M2GameBuoyancyStabilityOffsetMeters") != std::string::npos &&
        source.find("M2GameAnteyMassTuningKg / water.Config().densityKgPerCubicMeter") != std::string::npos &&
        source.find("17'400") == std::string::npos;
    return symmetric && productionSpatialPolicy;
}

bool IG1B1SubmergedSailDevicesUseOnlyOpaquePostTransforms()
{
    using DeepRun::Assets::ModelVector3;
    DeepRun::Assets::AssetManager assets(testAssetRoot);
    const auto definition = DeepRun::Game::Submarine::LoadProductionAnteyAssetDefinition(assets);
    const auto model = assets.LoadModel(AnteyModelPath);
    if (!definition || !model || definition->retractableSailDevices.size() != 9U)
    {
        return false;
    }

    std::set<std::string> semanticIds;
    std::set<std::size_t> bindingIndices;
    std::set<std::size_t> meshNodeIndices;
    std::vector<DeepRun::Render::ModelNodeTransformOverride> overrides;
    for (const auto& device : definition->retractableSailDevices)
    {
        if (device.defaultState != DeepRun::Game::Submarine::RetractableSailDeviceState::Stowed ||
            !semanticIds.insert(device.semanticId).second || !bindingIndices.insert(device.presentationNodeBindingIndex).second ||
            device.presentationNodeBindingIndex >= model->Get()->nodeBindings.size())
        {
            return false;
        }
        const auto meshNodeIndex = model->Get()->nodeBindings[device.presentationNodeBindingIndex].meshNodeIndex;
        if (!meshNodeIndex || *meshNodeIndex >= model->Get()->nodes.size())
        {
            return false;
        }
        if (!meshNodeIndices.insert(*meshNodeIndex).second)
        {
            return false;
        }
        overrides.push_back({.nodeIndex = *meshNodeIndex, .nodeLocalPostTransform = device.stowedLocalPostTransform});
    }

    const auto baseline = DeepRun::Render::PrepareModelDraws(*model->Get());
    const auto stowed = DeepRun::Render::PrepareModelDraws(*model->Get(), {}, overrides);
    if (!baseline || !stowed || baseline->size() != stowed->size())
    {
        return false;
    }
    for (std::size_t index = 0; index < baseline->size(); ++index)
    {
        if (!meshNodeIndices.contains(baseline->at(index).nodeIndex) &&
            baseline->at(index).modelToWorld.values != stowed->at(index).modelToWorld.values)
        {
            return false;
        }
    }

    const auto transformPoint = [](const DeepRun::Assets::ModelTransform& matrix, const ModelVector3& point)
    {
        return ModelVector3{
            .x = matrix.values[0] * point.x + matrix.values[4] * point.y + matrix.values[8] * point.z + matrix.values[12],
            .y = matrix.values[1] * point.x + matrix.values[5] * point.y + matrix.values[9] * point.z + matrix.values[13],
            .z = matrix.values[2] * point.x + matrix.values[6] * point.y + matrix.values[10] * point.z + matrix.values[14]};
    };
    float highestStowedY = -std::numeric_limits<float>::infinity();
    for (const auto& device : definition->retractableSailDevices)
    {
        const std::size_t nodeIndex = *model->Get()->nodeBindings[device.presentationNodeBindingIndex].meshNodeIndex;
        bool sawDevicePrimitive = false;
        for (const auto& draw : *stowed)
        {
            if (draw.nodeIndex != nodeIndex) continue;
            sawDevicePrimitive = true;
            for (const auto& vertex : model->Get()->primitives[draw.primitiveIndex].vertices)
            {
                highestStowedY = std::max(highestStowedY, transformPoint(draw.modelToWorld, vertex.position).y);
            }
        }
        if (!sawDevicePrimitive || highestStowedY > device.stowedSailEnvelopeMaximumY + 1.0e-3F)
        {
            return false;
        }
    }
    std::vector<std::pair<std::string, float>> allSailDeviceMaximumY;
    for (const auto& node : model->Get()->nodes)
    {
        if (node.name.find("SailDevice_") == std::string::npos)
        {
            continue;
        }
        const auto draw = std::find_if(stowed->begin(), stowed->end(), [&node, &model](const auto& candidate) {
            return candidate.nodeIndex == static_cast<std::size_t>(&node - model->Get()->nodes.data());
        });
        if (draw == stowed->end())
        {
            return false;
        }
        float maximumY = -std::numeric_limits<float>::infinity();
        for (const auto& vertex : model->Get()->primitives[draw->primitiveIndex].vertices)
        {
            maximumY = std::max(maximumY, transformPoint(draw->modelToWorld, vertex.position).y);
        }
        allSailDeviceMaximumY.emplace_back(node.name, maximumY);
    }
    std::sort(allSailDeviceMaximumY.begin(), allSailDeviceMaximumY.end(),
              [](const auto& left, const auto& right) { return left.second > right.second; });
    if (allSailDeviceMaximumY.size() != 20U)
    {
        return false;
    }
    const std::string playground = ReadFile(std::filesystem::path(DEEPRUN_SOURCE_ROOT) / "Game/PhysicalPlayground.cpp");
    std::cout << "[IG1-B.1 evidence] retractable devices=" << overrides.size()
              << ", highest stowed runtime Y=" << highestStowedY << ", all device maximum Y:";
    for (const auto& [name, maximumY] : allSailDeviceMaximumY)
    {
        std::cout << ' ' << name << '=' << maximumY;
    }
    std::cout << '\n';
    return playground.find("SailDevice_") == std::string::npos;
}

bool IG1A2ProductionSemanticSpatialMetadataIsGeometryDerived()
{
    DeepRun::Assets::AssetManager assets(testAssetRoot);
    const auto definition = DeepRun::Game::Submarine::LoadProductionAnteyAssetDefinition(assets);
    const auto model = assets.LoadModel(AnteyModelPath);
    if (!definition || !model || definition->propellers.size() != 2U || definition->compartments.size() != 10U ||
        definition->p700LaunchAnchors.size() != 24U || definition->torpedoLaunchAnchors.size() != 6U ||
        definition->retractableSailDevices.size() != 9U || definition->collisionProxies.empty() ||
        definition->buoyancyProxy.semanticId.empty())
    {
        return false;
    }
    std::set<std::string> propellerIds;
    std::set<std::size_t> propellerBindings;
    for (const auto& propeller : definition->propellers)
    {
        if (!propellerIds.insert(propeller.semanticId).second || propeller.presentationNodeBindingIndex >= model->Get()->nodeBindings.size() ||
            !std::isfinite(propeller.localOrigin.x) || !std::isfinite(propeller.localOrigin.y) ||
            !std::isfinite(propeller.localOrigin.z) || std::abs(propeller.localOrigin.x) < 0.02F ||
            !propellerBindings.insert(propeller.presentationNodeBindingIndex).second)
        {
            return false;
        }
    }
    const auto port = std::find_if(definition->propellers.begin(), definition->propellers.end(),
                                   [](const auto& value) { return value.semanticId == "propeller.port"; });
    const auto starboard = std::find_if(definition->propellers.begin(), definition->propellers.end(),
                                        [](const auto& value) { return value.semanticId == "propeller.starboard"; });
    if (propellerIds != std::set<std::string>{"propeller.port", "propeller.starboard"} ||
        port == definition->propellers.end() || starboard == definition->propellers.end() ||
        !(port->localOrigin.z < 0.0F && starboard->localOrigin.z > 0.0F) ||
        std::abs(port->localOrigin.x - starboard->localOrigin.x) > 0.02F)
    {
        return false;
    }
    std::set<std::string> compartmentIds;
    std::set<float> longitudinalCenters;
    for (const auto& compartment : definition->compartments)
    {
        if (!compartmentIds.insert(compartment.semanticId).second || !std::isfinite(compartment.localCenter.x) ||
            !std::isfinite(compartment.localCenter.y) || !std::isfinite(compartment.localCenter.z) ||
            !(compartment.halfExtents.x > 0.0F && compartment.halfExtents.y > 0.0F && compartment.halfExtents.z > 0.0F))
        {
            return false;
        }
        longitudinalCenters.insert(compartment.localCenter.x);
    }
    const std::string loader = ReadFile(std::filesystem::path(DEEPRUN_SOURCE_ROOT) / "Game/Submarine/ProductionAnteyAsset.cpp");
    std::cout << "[IG1-A.2 evidence] propeller runtime pivots port(" << port->localOrigin.x << ',' << port->localOrigin.y
              << ',' << port->localOrigin.z << ") starboard(" << starboard->localOrigin.x << ',' << starboard->localOrigin.y
              << ',' << starboard->localOrigin.z << "), compartment longitudinal centers=" << longitudinalCenters.size() << '\n';
    return longitudinalCenters.size() == 10U && loader.find("ends_with(\"_Port\")") == std::string::npos &&
           loader.find("ends_with(\"_Starboard\")") == std::string::npos;
}

bool IG1APublicSemanticDefinitionHasNoRawNodeNames()
{
    const std::string header = ReadFile(std::filesystem::path(DEEPRUN_SOURCE_ROOT) / "Game/Submarine/ProductionAnteyAsset.h");
    return header.find("SM_Propeller") == std::string::npos && header.find("HP_Antey_") == std::string::npos &&
           header.find("HP_P700") == std::string::npos && header.find("HP_TORPEDO") == std::string::npos;
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
    const auto silentHeadlessHaptic = engine.SubmitHapticEffect({
        .id = 1,
        .lowFrequencyMotor = 0.5F,
        .highFrequencyMotor = 0.25F,
        .durationSeconds = 0.1F,
        .priority = 10});
    if (!engine.ActiveScene().IsValid(entity) || !assetResult || !assetResult->IsValid() ||
        !silentHeadlessHaptic)
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
    const std::filesystem::path hdrPath = temporary.Path() / "hdr.json";
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
    WriteFile(
        hdrPath,
        R"({"renderer":{"vsync":true,"hdr":true,"width":1920,"height":1080},"physics":{"fixedHz":60}})");

    const auto valid = DeepRun::Core::LoadEngineConfig(validPath);
    const auto malformed = DeepRun::Core::LoadEngineConfig(malformedPath);
    const auto invalid = DeepRun::Core::LoadEngineConfig(invalidPath);
    const auto wrongType = DeepRun::Core::LoadEngineConfig(wrongTypePath);
    const auto hdr = DeepRun::Core::LoadEngineConfig(hdrPath);
    const auto missing = DeepRun::Core::LoadEngineConfig(temporary.Path() / "missing.json");
    return valid && valid->renderer.width == 1920 && valid->renderer.height == 1080 &&
           !valid->renderer.vsync && !valid->renderer.hdr && valid->physics.fixedHz == 120 && !malformed &&
           malformed.error().code == DeepRun::Core::ConfigErrorCode::InvalidJson && !invalid && !wrongType &&
           wrongType.error().code == DeepRun::Core::ConfigErrorCode::InvalidValue &&
           wrongType.error().message.find("renderer.vsync") != std::string::npos && hdr && hdr->renderer.hdr && !missing &&
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

bool I1SemanticAxisStorage()
{
    DeepRun::Input::InputState input;
    for (const float value : {-1.0F, 0.0F, 1.0F})
    {
        input.SetAxis(DeepRun::Input::InputAxis::Throttle, value);
        input.SetAxis(DeepRun::Input::InputAxis::Depth, value);
        if (input.Axis(DeepRun::Input::InputAxis::Throttle) != value ||
            input.Axis(DeepRun::Input::InputAxis::Depth) != value)
        {
            return false;
        }
        input.BeginFrame();
        if (input.Axis(DeepRun::Input::InputAxis::Throttle) != value ||
            input.Axis(DeepRun::Input::InputAxis::Depth) != value)
        {
            return false;
        }
    }
    return true;
}

bool I1KeyboardSemanticMapping()
{
    DeepRun::Diagnostics::Logger logger;
    // Unit tests use the pure semantic/event path; no physical Windows gamepad polling or output is performed.
    DeepRun::Input::InputSystem input(logger, false);
    const auto key = [](const DeepRun::Platform::WindowEventType type, const DeepRun::Platform::Key value) {
        return DeepRun::Platform::WindowEvent{.type = type, .key = value};
    };
    std::vector<DeepRun::Platform::WindowEvent> events{
        key(DeepRun::Platform::WindowEventType::KeyDown, DeepRun::Platform::Key::D)};
    input.ProcessEvents(events);
    if (input.State().Axis(DeepRun::Input::InputAxis::Throttle) != 1.0F)
    {
        return false;
    }
    events = {key(DeepRun::Platform::WindowEventType::KeyUp, DeepRun::Platform::Key::D)};
    input.ProcessEvents(events);
    if (input.State().Axis(DeepRun::Input::InputAxis::Throttle) != 0.0F)
    {
        return false;
    }
    events = {key(DeepRun::Platform::WindowEventType::KeyDown, DeepRun::Platform::Key::A)};
    input.ProcessEvents(events);
    if (input.State().Axis(DeepRun::Input::InputAxis::Throttle) != -1.0F)
    {
        return false;
    }
    events = {key(DeepRun::Platform::WindowEventType::KeyDown, DeepRun::Platform::Key::D)};
    input.ProcessEvents(events);
    if (input.State().Axis(DeepRun::Input::InputAxis::Throttle) != 0.0F)
    {
        return false;
    }

    events = {key(DeepRun::Platform::WindowEventType::KeyDown, DeepRun::Platform::Key::S)};
    input.ProcessEvents(events);
    if (input.State().Axis(DeepRun::Input::InputAxis::Depth) != 1.0F)
    {
        return false;
    }
    events = {key(DeepRun::Platform::WindowEventType::KeyUp, DeepRun::Platform::Key::S),
              key(DeepRun::Platform::WindowEventType::KeyDown, DeepRun::Platform::Key::W)};
    input.ProcessEvents(events);
    if (input.State().Axis(DeepRun::Input::InputAxis::Depth) != -1.0F)
    {
        return false;
    }
    events = {key(DeepRun::Platform::WindowEventType::KeyDown, DeepRun::Platform::Key::S)};
    input.ProcessEvents(events);
    return input.State().Axis(DeepRun::Input::InputAxis::Depth) == 0.0F;
}

bool I1ControllerSemanticMapping()
{
    const auto positiveX = DeepRun::Input::MapControllerLeftStick(1.0F, 0.0F);
    const auto negativeX = DeepRun::Input::MapControllerLeftStick(-1.0F, 0.0F);
    const auto up = DeepRun::Input::MapControllerLeftStick(0.0F, 1.0F);
    const auto down = DeepRun::Input::MapControllerLeftStick(0.0F, -1.0F);
    const auto nearCenter = DeepRun::Input::MapControllerLeftStick(0.05F, -0.05F);
    const auto diagonal = DeepRun::Input::MapControllerLeftStick(0.8F, -0.8F);
    return std::abs(positiveX.throttle - 1.0F) < 1.0e-5F &&
           std::abs(negativeX.throttle + 1.0F) < 1.0e-5F && std::abs(up.depth + 1.0F) < 1.0e-5F &&
           std::abs(down.depth - 1.0F) < 1.0e-5F && nearCenter.throttle == 0.0F && nearCenter.depth == 0.0F &&
           std::isfinite(diagonal.throttle) && std::isfinite(diagonal.depth) &&
           std::abs(diagonal.throttle) <= 1.0F && std::abs(diagonal.depth) <= 1.0F;
}

bool I1ControllerDisconnectAndArbitration()
{
    const DeepRun::Input::GamepadState disconnected{.connected = false, .leftX = 1.0F, .leftY = -1.0F};
    const auto disconnectedAxes = DeepRun::Input::SemanticAxesForGamepad(disconnected);
    const float controller = 0.4F;
    return disconnectedAxes.throttle == 0.0F && disconnectedAxes.depth == 0.0F &&
           DeepRun::Input::ResolveSemanticAxis(false, false, controller) == controller &&
           DeepRun::Input::ResolveSemanticAxis(true, false, controller) == -1.0F &&
           DeepRun::Input::ResolveSemanticAxis(true, true, controller) == 0.0F &&
           DeepRun::Input::ResolveSemanticAxis(false, false, controller) == controller &&
           DeepRun::Input::ResolveSemanticAxis(false, true, -0.25F) == 1.0F &&
           DeepRun::Input::ResolveSemanticAxis(true, false, -0.25F) == -1.0F;
}

bool WgiGamepadReadingConvertsToGenericState()
{
    using DeepRun::Input::GamepadButton;
    using DeepRun::Input::Windows::PublishWindowsGamepadState;
    using DeepRun::Input::Windows::WindowsGamepadReading;
    constexpr std::uint32_t WgiMenu = 1U << 0U;
    constexpr std::uint32_t WgiA = 1U << 2U;
    constexpr std::uint32_t WgiDpadLeft = 1U << 8U;
    constexpr std::uint32_t WgiRightShoulder = 1U << 11U;
    const auto state = PublishWindowsGamepadState(
        true,
        true,
        WindowsGamepadReading{
            .leftX = 0.75,
            .leftY = -0.50,
            .rightX = -0.25,
            .rightY = 0.125,
            .leftTrigger = 0.20,
            .rightTrigger = 0.90,
            .buttons = WgiMenu | WgiA | WgiDpadLeft | WgiRightShoulder});
    const std::uint16_t expectedButtons = static_cast<std::uint16_t>(GamepadButton::Start) |
                                          static_cast<std::uint16_t>(GamepadButton::A) |
                                          static_cast<std::uint16_t>(GamepadButton::DpadLeft) |
                                          static_cast<std::uint16_t>(GamepadButton::RightShoulder);
    return state.connected && std::abs(state.leftX - 0.75F) < 1.0e-6F &&
           std::abs(state.leftY + 0.50F) < 1.0e-6F && std::abs(state.rightX + 0.25F) < 1.0e-6F &&
           std::abs(state.rightY - 0.125F) < 1.0e-6F && std::abs(state.leftTrigger - 0.20F) < 1.0e-6F &&
           std::abs(state.rightTrigger - 0.90F) < 1.0e-6F && state.buttons == expectedButtons;
}

bool WgiConnectionAndFocusNeutralization()
{
    using DeepRun::Input::Windows::PublishWindowsGamepadState;
    using DeepRun::Input::Windows::WindowsGamepadReading;
    const WindowsGamepadReading live{
        .leftX = 0.8,
        .leftY = -0.4,
        .rightX = 0.0,
        .rightY = 0.0,
        .leftTrigger = 0.0,
        .rightTrigger = 0.0,
        .buttons = 0};
    const auto disconnected = PublishWindowsGamepadState(false, true, live);
    const auto connected = PublishWindowsGamepadState(true, true, live);
    const auto unfocused = PublishWindowsGamepadState(true, false, live);
    const auto unavailable = PublishWindowsGamepadState(true, true, std::nullopt);
    const auto resumed = PublishWindowsGamepadState(
        true,
        true,
        WindowsGamepadReading{
            .leftX = -0.6,
            .leftY = 0.3,
            .rightX = 0.0,
            .rightY = 0.0,
            .leftTrigger = 0.0,
            .rightTrigger = 0.0,
            .buttons = 0});
    return !disconnected.connected && disconnected.leftX == 0.0F && disconnected.leftY == 0.0F &&
           connected.connected && std::abs(connected.leftX - 0.8F) < 1.0e-6F &&
           std::abs(connected.leftY + 0.4F) < 1.0e-6F && unfocused.connected && unfocused.leftX == 0.0F &&
           unfocused.leftY == 0.0F && unavailable.connected && unavailable.leftX == 0.0F &&
           unavailable.leftY == 0.0F && resumed.connected && std::abs(resumed.leftX + 0.6F) < 1.0e-6F &&
           std::abs(resumed.leftY - 0.3F) < 1.0e-6F;
}

bool I1VesselCommandMapsSemanticAxesAndRejectsMalformedInput()
{
    DeepRun::Input::InputState input;
    input.SetAxis(DeepRun::Input::InputAxis::Throttle, 0.7F);
    input.SetAxis(DeepRun::Input::InputAxis::Depth, -0.25F);
    const auto mapped = DeepRun::Game::VesselCommandStateFromInput(input);
    if (!mapped || std::abs(mapped->throttleFraction - 0.7F) > 1.0e-5F ||
        std::abs(mapped->depthCommandFraction + 0.25F) > 1.0e-5F)
    {
        return false;
    }
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    return !DeepRun::Game::ValidateVesselCommandState({.throttleFraction = nan}) &&
           !DeepRun::Game::ValidateVesselCommandState({.depthCommandFraction = infinity}) &&
           !DeepRun::Game::ValidateVesselCommandState({.throttleFraction = -1.01F}) &&
           !DeepRun::Game::ValidateVesselCommandState({.depthCommandFraction = 1.01F});
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

// J: zero force is a true no-op — it must NOT wake a sleeping body (symmetric to the non-zero activation test).
bool ForceZeroDoesNotWakeSleepingBody()
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
        return false; // the body must have gone to sleep before we test the no-op branch
    }

    // Zero force at a finite position: accepted, but it must not touch Jolt's activation path.
    const DeepRun::Physics::PhysicsVector3 zero{0.0F, 0.0F, 0.0F};
    const DeepRun::Physics::PhysicsVector3 com{sleeping->position.x, sleeping->position.y, sleeping->position.z};
    if (!world.AddForceAtWorldPosition(handle, zero, com))
    {
        return false; // zero force must succeed (not be rejected as malformed)
    }

    const auto immediatelyAfter = world.GetBodyState(handle);
    if (!immediatelyAfter || immediatelyAfter->active)
    {
        return false; // the no-op call must not wake the sleeping body
    }
    if (immediatelyAfter->linearVelocity.x != 0.0F || immediatelyAfter->position.x != com.x)
    {
        return false; // velocity and position must be unchanged by the zero-force call
    }

    world.Step(1.0F / 60.0F); // one more force-free step: the body must stay asleep and still

    const auto after = world.GetBodyState(handle);
    if (!after || after->active)
    {
        return false;
    }
    return after->linearVelocity.x == 0.0F && after->position.x == com.x && StateIsFinite(*after);
}

// ---------------------------------------------------------------------------
// M2 Slice F2: generic transient torque PhysicsWorld API (headless, public API only).
// ---------------------------------------------------------------------------

bool TorqueSignAndOrientation()
{
    DeepRun::Diagnostics::Logger logger;
    DeepRun::Physics::PhysicsWorld world(logger);
    if (!world.Initialize())
    {
        return false;
    }

    auto positiveInfo = ForceTestBodyInfo();
    auto negativeInfo = ForceTestBodyInfo();
    positiveInfo.position = {-100.0F, 0.0F, 0.0F};
    negativeInfo.position = {100.0F, 0.0F, 0.0F};
    const auto positive = world.CreateDynamicBoxBody(positiveInfo);
    const auto negative = world.CreateDynamicBoxBody(negativeInfo);
    const auto initialPositive = world.GetBodyState(positive);
    const auto initialNegative = world.GetBodyState(negative);
    if (!initialPositive || !initialNegative ||
        !world.AddTorque(positive, {0.0F, 0.0F, 16.0F}) ||
        !world.AddTorque(negative, {0.0F, 0.0F, -16.0F}))
    {
        return false;
    }
    world.Step(1.0F / 60.0F);

    const auto afterPositive = world.GetBodyState(positive);
    const auto afterNegative = world.GetBodyState(negative);
    return afterPositive && afterNegative && StateIsFinite(*afterPositive) && StateIsFinite(*afterNegative) &&
           afterPositive->angularVelocity.z > 0.0F && afterNegative->angularVelocity.z < 0.0F &&
           afterPositive->orientation.z > initialPositive->orientation.z &&
           afterNegative->orientation.z < initialNegative->orientation.z &&
           afterPositive->linearVelocity == DeepRun::Physics::PhysicsVector3{} &&
           afterNegative->linearVelocity == DeepRun::Physics::PhysicsVector3{};
}

bool TorqueMultipleCallsAccumulate()
{
    DeepRun::Diagnostics::Logger logger;
    DeepRun::Physics::PhysicsWorld world(logger);
    if (!world.Initialize())
    {
        return false;
    }

    auto infoA = ForceTestBodyInfo();
    auto infoB = ForceTestBodyInfo();
    infoA.position = {-100.0F, 0.0F, 0.0F};
    infoB.position = {100.0F, 0.0F, 0.0F};
    const auto handleA = world.CreateDynamicBoxBody(infoA);
    const auto handleB = world.CreateDynamicBoxBody(infoB);
    if (!handleA.IsValid() || !handleB.IsValid() ||
        !world.AddTorque(handleA, {0.0F, 0.0F, 8.0F}) ||
        !world.AddTorque(handleA, {0.0F, 0.0F, 8.0F}) ||
        !world.AddTorque(handleB, {0.0F, 0.0F, 16.0F}))
    {
        return false;
    }
    world.Step(1.0F / 60.0F);

    const auto afterA = world.GetBodyState(handleA);
    const auto afterB = world.GetBodyState(handleB);
    return afterA && afterB && afterA->angularVelocity.z > 0.0F &&
           std::abs(afterA->angularVelocity.z - afterB->angularVelocity.z) < 1.0e-4F &&
           std::abs(afterA->orientation.z - afterB->orientation.z) < 1.0e-4F &&
           StateIsFinite(*afterA) && StateIsFinite(*afterB);
}

bool TorqueIsNotPersistent()
{
    DeepRun::Diagnostics::Logger logger;
    DeepRun::Physics::PhysicsWorld world(logger);
    if (!world.Initialize())
    {
        return false;
    }

    const auto handle = world.CreateDynamicBoxBody(ForceTestBodyInfo());
    if (!handle.IsValid() || !world.AddTorque(handle, {0.0F, 0.0F, 16.0F}))
    {
        return false;
    }
    world.Step(1.0F / 60.0F);
    const auto afterFirst = world.GetBodyState(handle);
    if (!afterFirst || afterFirst->angularVelocity.z <= 0.0F)
    {
        return false;
    }

    world.Step(1.0F / 60.0F); // no re-application: no second angular-velocity increment
    const auto afterSecond = world.GetBodyState(handle);
    return afterSecond && StateIsFinite(*afterSecond) &&
           std::abs(afterSecond->angularVelocity.z - afterFirst->angularVelocity.z) < 1.0e-4F &&
           afterSecond->orientation.z > afterFirst->orientation.z;
}

bool TorqueInvalidInputAndZeroNoOp()
{
    DeepRun::Diagnostics::Logger logger;
    DeepRun::Physics::PhysicsError error;
    const DeepRun::Physics::PhysicsVector3 torque{0.0F, 0.0F, 10.0F};

    // Validation order: an uninitialized world reports NotInitialized before examining the handle.
    {
        DeepRun::Physics::PhysicsWorld uninitialized(logger);
        if (uninitialized.AddTorque({}, torque, &error) ||
            error.code != DeepRun::Physics::PhysicsErrorCode::NotInitialized)
        {
            return false;
        }
    }

    // Capture a live handle from a different world identity, then let that world release Jolt's global instance.
    DeepRun::Physics::PhysicsBodyHandle foreign;
    {
        DeepRun::Physics::PhysicsWorld owner(logger);
        if (!owner.Initialize())
        {
            return false;
        }
        foreign = owner.CreateDynamicBoxBody(ForceTestBodyInfo());
        if (!foreign.IsValid())
        {
            return false;
        }
    }

    DeepRun::Physics::PhysicsWorld world(logger);
    if (!world.Initialize() || world.AddTorque(foreign, torque, &error) ||
        error.code != DeepRun::Physics::PhysicsErrorCode::InvalidHandle)
    {
        return false;
    }
    if (world.AddTorque({}, torque, &error) || error.code != DeepRun::Physics::PhysicsErrorCode::InvalidHandle)
    {
        return false;
    }

    const auto stale = world.CreateDynamicBoxBody(ForceTestBodyInfo());
    if (!stale.IsValid() || !world.DestroyBody(stale) || world.AddTorque(stale, torque, &error) ||
        error.code != DeepRun::Physics::PhysicsErrorCode::InvalidHandle)
    {
        return false;
    }

    const auto handle = world.CreateDynamicBoxBody(ForceTestBodyInfo());
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    for (const DeepRun::Physics::PhysicsVector3 invalid :
         {DeepRun::Physics::PhysicsVector3{nan, 0.0F, 0.0F},
          DeepRun::Physics::PhysicsVector3{0.0F, infinity, 0.0F},
          DeepRun::Physics::PhysicsVector3{0.0F, 0.0F, -infinity}})
    {
        if (world.AddTorque(handle, invalid, &error) ||
            error.code != DeepRun::Physics::PhysicsErrorCode::InvalidInput)
        {
            return false;
        }
    }

    const auto before = world.GetBodyState(handle);
    if (!before || !world.AddTorque(handle, {}))
    {
        return false;
    }
    world.Step(1.0F / 60.0F);
    const auto after = world.GetBodyState(handle);
    return after && StateIsFinite(*after) && after->position == before->position &&
           after->orientation == before->orientation && after->linearVelocity == before->linearVelocity &&
           after->angularVelocity == before->angularVelocity;
}

bool TorqueActivatesSleepingBody()
{
    DeepRun::Diagnostics::Logger logger;
    DeepRun::Physics::PhysicsWorld world(logger);
    if (!world.Initialize())
    {
        return false;
    }
    const auto handle = world.CreateDynamicBoxBody(ForceTestBodyInfo());
    for (int step = 0; step < 120; ++step)
    {
        world.Step(1.0F / 60.0F);
    }
    const auto sleeping = world.GetBodyState(handle);
    if (!sleeping || sleeping->active || !world.AddTorque(handle, {0.0F, 0.0F, 16.0F}))
    {
        return false;
    }
    world.Step(1.0F / 60.0F);
    const auto after = world.GetBodyState(handle);
    return after && after->active && after->angularVelocity.z > 0.0F && StateIsFinite(*after);
}

bool TorqueZeroDoesNotWakeSleepingBody()
{
    DeepRun::Diagnostics::Logger logger;
    DeepRun::Physics::PhysicsWorld world(logger);
    if (!world.Initialize())
    {
        return false;
    }
    const auto handle = world.CreateDynamicBoxBody(ForceTestBodyInfo());
    for (int step = 0; step < 120; ++step)
    {
        world.Step(1.0F / 60.0F);
    }
    const auto sleeping = world.GetBodyState(handle);
    if (!sleeping || sleeping->active || !world.AddTorque(handle, {}))
    {
        return false;
    }
    const auto immediate = world.GetBodyState(handle);
    if (!immediate || immediate->active ||
        immediate->angularVelocity != DeepRun::Physics::PhysicsVector3{})
    {
        return false;
    }
    world.Step(1.0F / 60.0F);
    const auto after = world.GetBodyState(handle);
    return after && !after->active && after->position == sleeping->position &&
           after->orientation == sleeping->orientation &&
           after->angularVelocity == DeepRun::Physics::PhysicsVector3{};
}

bool TorquePlanarDOFPreservation()
{
    DeepRun::Diagnostics::Logger logger;
    DeepRun::Physics::PhysicsWorld world(logger);
    if (!world.Initialize())
    {
        return false;
    }
    auto info = ForceTestBodyInfo();
    DeepRun::Physics::PhysicsDegreesOfFreedom planar;
    planar.translationZ = false;
    planar.rotationX = false;
    planar.rotationY = false;
    info.degreesOfFreedom = planar;
    const auto handle = world.CreateDynamicBoxBody(info);
    const auto initial = world.GetBodyState(handle);
    if (!initial || !world.AddTorque(handle, {16.0F, 16.0F, 16.0F}))
    {
        return false;
    }
    world.Step(1.0F / 60.0F);
    const auto after = world.GetBodyState(handle);
    return after && StateIsFinite(*after) && std::abs(after->angularVelocity.x) < 1.0e-5F &&
           std::abs(after->angularVelocity.y) < 1.0e-5F && after->angularVelocity.z > 0.0F &&
           after->position == initial->position &&
           after->linearVelocity == DeepRun::Physics::PhysicsVector3{};
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
// M2 Slice E2: pure multi-point buoyancy model and force calculation. These tests construct no Jolt world
// and exercise only WaterBody + marine data + a body pose + an explicit gravity magnitude.
// ---------------------------------------------------------------------------

namespace
{
using DeepRun::Marine::BuoyancyComponent;
using DeepRun::Marine::BuoyancyErrorCode;
using DeepRun::Marine::BuoyancyPoint;
using DeepRun::Marine::BuoyancyPose;
using DeepRun::Marine::BuoyancyResult;
using DeepRun::Marine::BuoyancySystem;
using DeepRun::Physics::PhysicsQuaternion;
using DeepRun::Physics::PhysicsVector3;

bool E2Near(const float actual, const float expected, const float tolerance = 1.0e-5F)
{
    const float scale = std::max(1.0F, std::abs(expected));
    return std::abs(actual - expected) <= tolerance * scale;
}

bool E2VectorNear(
    const PhysicsVector3& actual,
    const PhysicsVector3& expected,
    const float tolerance = 1.0e-5F)
{
    return E2Near(actual.x, expected.x, tolerance) && E2Near(actual.y, expected.y, tolerance) &&
           E2Near(actual.z, expected.z, tolerance);
}

bool E2HasError(
    const std::expected<BuoyancyResult, DeepRun::Marine::BuoyancyError>& result,
    const BuoyancyErrorCode code)
{
    return !result && result.error().code == code && !result.error().message.empty();
}

BuoyancyPoint E2Point(
    const PhysicsVector3 bodyLocalPositionMeters,
    const float displacedVolumeCubicMeters = 1.0F,
    const float submersionHalfHeightMeters = 1.0F)
{
    return BuoyancyPoint{
        .bodyLocalPositionMeters = bodyLocalPositionMeters,
        .displacedVolumeCubicMeters = displacedVolumeCubicMeters,
        .submersionHalfHeightMeters = submersionHalfHeightMeters};
}
} // namespace

bool BuoyancyRejectsInvalidConfiguration()
{
    const auto water = DeepRun::Marine::WaterBody::Create(
        {.surfaceLevelY = 0.0F, .densityKgPerCubicMeter = 1000.0F});
    if (!water)
    {
        return false;
    }
    const BuoyancyPose pose{};
    if (!E2HasError(BuoyancySystem::Calculate(*water, {}, pose, 10.0F),
                    BuoyancyErrorCode::InvalidConfiguration))
    {
        return false;
    }

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    const PhysicsVector3 invalidPositions[] = {
        {nan, 0.0F, 0.0F}, {0.0F, nan, 0.0F}, {0.0F, 0.0F, nan},
        {infinity, 0.0F, 0.0F}, {0.0F, -infinity, 0.0F}, {0.0F, 0.0F, infinity}};
    for (const PhysicsVector3 position : invalidPositions)
    {
        const BuoyancyComponent component{.points = {E2Point(position)}};
        if (!E2HasError(BuoyancySystem::Calculate(*water, component, pose, 10.0F),
                        BuoyancyErrorCode::InvalidConfiguration))
        {
            return false;
        }
    }

    for (const float volume : {0.0F, -1.0F, nan, infinity})
    {
        const BuoyancyComponent component{.points = {E2Point({}, volume, 1.0F)}};
        if (!E2HasError(BuoyancySystem::Calculate(*water, component, pose, 10.0F),
                        BuoyancyErrorCode::InvalidConfiguration))
        {
            return false;
        }
    }
    for (const float halfHeight : {0.0F, -1.0F, nan, infinity})
    {
        const BuoyancyComponent component{.points = {E2Point({}, 1.0F, halfHeight)}};
        if (!E2HasError(BuoyancySystem::Calculate(*water, component, pose, 10.0F),
                        BuoyancyErrorCode::InvalidConfiguration))
        {
            return false;
        }
    }
    return true;
}

bool BuoyancyRejectsInvalidPoseAndGravity()
{
    const auto water = DeepRun::Marine::WaterBody::Create(
        {.surfaceLevelY = 0.0F, .densityKgPerCubicMeter = 1000.0F});
    if (!water)
    {
        return false;
    }
    const BuoyancyComponent component{.points = {E2Point({})}};
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();

    const PhysicsVector3 invalidPositions[] = {
        {nan, 0.0F, 0.0F}, {0.0F, nan, 0.0F}, {0.0F, 0.0F, nan},
        {infinity, 0.0F, 0.0F}, {0.0F, -infinity, 0.0F}, {0.0F, 0.0F, infinity}};
    for (const PhysicsVector3 position : invalidPositions)
    {
        const BuoyancyPose pose{.worldPositionMeters = position};
        if (!E2HasError(BuoyancySystem::Calculate(*water, component, pose, 10.0F),
                        BuoyancyErrorCode::InvalidPose))
        {
            return false;
        }
    }

    const PhysicsQuaternion invalidOrientations[] = {
        {0.0F, 0.0F, 0.0F, 0.0F}, {nan, 0.0F, 0.0F, 1.0F},
        {0.0F, infinity, 0.0F, 1.0F}, {0.0F, 0.0F, -infinity, 1.0F}};
    for (const PhysicsQuaternion orientation : invalidOrientations)
    {
        const BuoyancyPose pose{.worldPositionMeters = {}, .worldOrientation = orientation};
        if (!E2HasError(BuoyancySystem::Calculate(*water, component, pose, 10.0F),
                        BuoyancyErrorCode::InvalidPose))
        {
            return false;
        }
    }

    const BuoyancyPose pose{};
    for (const float gravity : {0.0F, -1.0F, nan, infinity})
    {
        if (!E2HasError(BuoyancySystem::Calculate(*water, component, pose, gravity),
                        BuoyancyErrorCode::InvalidGravity))
        {
            return false;
        }
    }
    return true;
}

bool BuoyancyFullyDryKeepsPointOrder()
{
    const auto water = DeepRun::Marine::WaterBody::Create(
        {.surfaceLevelY = 0.0F, .densityKgPerCubicMeter = 1000.0F});
    const BuoyancyComponent component{
        .points = {E2Point({-3.0F, 0.0F, 0.0F}, 1.0F, 1.0F),
                   E2Point({2.0F, 1.0F, 4.0F}, 2.0F, 1.0F)}};
    const BuoyancyPose pose{.worldPositionMeters = {10.0F, 5.0F, -2.0F}};
    const auto result = BuoyancySystem::Calculate(*water, component, pose, 10.0F);
    if (!result || result->points.size() != component.points.size())
    {
        return false;
    }
    return E2VectorNear(result->points[0].worldPositionMeters, {7.0F, 5.0F, -2.0F}) &&
           E2VectorNear(result->points[1].worldPositionMeters, {12.0F, 6.0F, 2.0F}) &&
           result->points[0].submergedFraction == 0.0F && result->points[1].submergedFraction == 0.0F &&
           result->points[0].submergedVolumeCubicMeters == 0.0F &&
           result->points[1].submergedVolumeCubicMeters == 0.0F &&
           result->points[0].forceNewtons == PhysicsVector3{} &&
           result->points[1].forceNewtons == PhysicsVector3{} &&
           result->totalForceNewtons == PhysicsVector3{} && result->totalSubmergedVolumeCubicMeters == 0.0F;
}

bool BuoyancyFullySubmergedSymmetricPoints()
{
    const auto water = DeepRun::Marine::WaterBody::Create(
        {.surfaceLevelY = 0.0F, .densityKgPerCubicMeter = 1000.0F});
    const BuoyancyComponent component{
        .points = {E2Point({2.0F, 0.0F, 0.0F}, 1.5F, 1.0F),
                   E2Point({-2.0F, 0.0F, 0.0F}, 1.5F, 1.0F)}};
    const BuoyancyPose pose{.worldPositionMeters = {0.0F, -3.0F, 0.0F}};
    const auto result = BuoyancySystem::Calculate(*water, component, pose, 10.0F);
    if (!result || result->points.size() != 2)
    {
        return false;
    }
    return E2VectorNear(result->points[0].worldPositionMeters, {2.0F, -3.0F, 0.0F}) &&
           E2VectorNear(result->points[1].worldPositionMeters, {-2.0F, -3.0F, 0.0F}) &&
           result->points[0].submergedFraction == 1.0F && result->points[1].submergedFraction == 1.0F &&
           E2Near(result->points[0].submergedVolumeCubicMeters, 1.5F) &&
           E2Near(result->points[1].submergedVolumeCubicMeters, 1.5F) &&
           E2VectorNear(result->points[0].forceNewtons, {0.0F, 15000.0F, 0.0F}) &&
           E2VectorNear(result->points[1].forceNewtons, {0.0F, 15000.0F, 0.0F}) &&
           E2VectorNear(result->totalForceNewtons, {0.0F, 30000.0F, 0.0F}) &&
           E2Near(result->totalSubmergedVolumeCubicMeters, 3.0F);
}

bool BuoyancyExactSurfaceIsHalfSubmerged()
{
    const auto water = DeepRun::Marine::WaterBody::Create(
        {.surfaceLevelY = 0.0F, .densityKgPerCubicMeter = 1000.0F});
    const BuoyancyComponent component{.points = {E2Point({}, 2.0F, 3.0F)}};
    const auto result = BuoyancySystem::Calculate(*water, component, {}, 10.0F);
    return result && result->points.size() == 1 && result->points[0].signedDepthMeters == 0.0F &&
           result->points[0].submergedFraction == 0.5F &&
           result->points[0].submergedVolumeCubicMeters == 1.0F &&
           E2VectorNear(result->points[0].forceNewtons, {0.0F, 10000.0F, 0.0F}) &&
           E2VectorNear(result->totalForceNewtons, result->points[0].forceNewtons);
}

bool BuoyancyQuarterAndThreeQuarterSubmersion()
{
    const auto water = DeepRun::Marine::WaterBody::Create(
        {.surfaceLevelY = 0.0F, .densityKgPerCubicMeter = 1000.0F});
    const BuoyancyComponent component{
        .points = {E2Point({0.0F, 1.0F, 0.0F}, 4.0F, 2.0F),
                   E2Point({0.0F, -1.0F, 0.0F}, 4.0F, 2.0F)}};
    const auto result = BuoyancySystem::Calculate(*water, component, {}, 10.0F);
    if (!result || result->points.size() != 2)
    {
        return false;
    }
    return result->points[0].signedDepthMeters == -1.0F && result->points[0].submergedFraction == 0.25F &&
           result->points[0].submergedVolumeCubicMeters == 1.0F &&
           E2VectorNear(result->points[0].forceNewtons, {0.0F, 10000.0F, 0.0F}) &&
           result->points[1].signedDepthMeters == 1.0F && result->points[1].submergedFraction == 0.75F &&
           result->points[1].submergedVolumeCubicMeters == 3.0F &&
           E2VectorNear(result->points[1].forceNewtons, {0.0F, 30000.0F, 0.0F}) &&
           E2Near(result->totalSubmergedVolumeCubicMeters, 4.0F) &&
           E2VectorNear(result->totalForceNewtons, {0.0F, 40000.0F, 0.0F});
}

bool BuoyancyShiftedSurfaceRegression()
{
    const auto zeroWater = DeepRun::Marine::WaterBody::Create(
        {.surfaceLevelY = 0.0F, .densityKgPerCubicMeter = 1000.0F});
    const auto shiftedWater = DeepRun::Marine::WaterBody::Create(
        {.surfaceLevelY = 50.0F, .densityKgPerCubicMeter = 1000.0F});
    const BuoyancyComponent component{
        .points = {E2Point({0.0F, 1.0F, 0.0F}, 2.0F, 2.0F),
                   E2Point({0.0F, -1.0F, 0.0F}, 2.0F, 2.0F)}};
    const auto atZero = BuoyancySystem::Calculate(*zeroWater, component, {}, 10.0F);
    const auto shifted = BuoyancySystem::Calculate(
        *shiftedWater, component, {.worldPositionMeters = {0.0F, 50.0F, 0.0F}}, 10.0F);
    if (!atZero || !shifted || atZero->points.size() != shifted->points.size())
    {
        return false;
    }
    for (std::size_t index = 0; index < atZero->points.size(); ++index)
    {
        if (!E2Near(shifted->points[index].worldPositionMeters.y,
                    atZero->points[index].worldPositionMeters.y + 50.0F) ||
            shifted->points[index].signedDepthMeters != atZero->points[index].signedDepthMeters ||
            shifted->points[index].submergedFraction != atZero->points[index].submergedFraction ||
            !E2VectorNear(shifted->points[index].forceNewtons, atZero->points[index].forceNewtons))
        {
            return false;
        }
    }
    return E2VectorNear(shifted->totalForceNewtons, atZero->totalForceNewtons) &&
           E2Near(shifted->totalSubmergedVolumeCubicMeters, atZero->totalSubmergedVolumeCubicMeters);
}

bool BuoyancyBodyLocalOrientationConvention()
{
    const auto water = DeepRun::Marine::WaterBody::Create(
        {.surfaceLevelY = 100.0F, .densityKgPerCubicMeter = 1000.0F});
    const BuoyancyComponent component{
        .points = {E2Point({2.0F, 0.0F, 0.0F}), E2Point({-2.0F, 0.0F, 0.0F})}};
    const BuoyancyPose identity{.worldPositionMeters = {10.0F, 20.0F, 30.0F}};
    const auto level = BuoyancySystem::Calculate(*water, component, identity, 10.0F);
    const float halfSqrtTwo = std::sqrt(0.5F);
    const BuoyancyPose rotated{
        .worldPositionMeters = {10.0F, 20.0F, 30.0F},
        .worldOrientation = {0.0F, 0.0F, halfSqrtTwo, halfSqrtTwo}};
    const auto quarterTurn = BuoyancySystem::Calculate(*water, component, rotated, 10.0F);
    return level && quarterTurn &&
           E2VectorNear(level->points[0].worldPositionMeters, {12.0F, 20.0F, 30.0F}) &&
           E2VectorNear(level->points[1].worldPositionMeters, {8.0F, 20.0F, 30.0F}) &&
           E2VectorNear(quarterTurn->points[0].worldPositionMeters, {10.0F, 22.0F, 30.0F}) &&
           E2VectorNear(quarterTurn->points[1].worldPositionMeters, {10.0F, 18.0F, 30.0F});
}

bool BuoyancyQuaternionSignEquivalence()
{
    const auto water = DeepRun::Marine::WaterBody::Create(
        {.surfaceLevelY = 0.0F, .densityKgPerCubicMeter = 1000.0F});
    const BuoyancyComponent component{
        .points = {E2Point({3.0F, 1.0F, -2.0F}, 2.0F, 4.0F),
                   E2Point({-1.0F, -2.0F, 5.0F}, 3.0F, 4.0F)}};
    const BuoyancyPose positive{
        .worldPositionMeters = {7.0F, -1.0F, 9.0F}, .worldOrientation = {0.0F, 0.0F, 2.0F, 2.0F}};
    const BuoyancyPose negative{
        .worldPositionMeters = positive.worldPositionMeters, .worldOrientation = {0.0F, 0.0F, -2.0F, -2.0F}};
    const auto first = BuoyancySystem::Calculate(*water, component, positive, 10.0F);
    const auto second = BuoyancySystem::Calculate(*water, component, negative, 10.0F);
    if (!first || !second || first->points.size() != second->points.size())
    {
        return false;
    }
    for (std::size_t index = 0; index < first->points.size(); ++index)
    {
        const auto& a = first->points[index];
        const auto& b = second->points[index];
        if (!E2VectorNear(a.worldPositionMeters, b.worldPositionMeters) ||
            !E2Near(a.signedDepthMeters, b.signedDepthMeters) ||
            !E2Near(a.submergedFraction, b.submergedFraction) ||
            !E2Near(a.submergedVolumeCubicMeters, b.submergedVolumeCubicMeters) ||
            !E2VectorNear(a.forceNewtons, b.forceNewtons))
        {
            return false;
        }
    }
    return E2VectorNear(first->totalForceNewtons, second->totalForceNewtons) &&
           E2Near(first->totalSubmergedVolumeCubicMeters, second->totalSubmergedVolumeCubicMeters);
}

bool BuoyancyPitchSensitiveBowAndStern()
{
    const auto water = DeepRun::Marine::WaterBody::Create(
        {.surfaceLevelY = 0.0F, .densityKgPerCubicMeter = 1000.0F});
    const BuoyancyComponent component{
        .points = {E2Point({2.0F, 0.0F, 0.0F}, 1.0F, 2.0F),
                   E2Point({-2.0F, 0.0F, 0.0F}, 1.0F, 2.0F)}};
    const auto level = BuoyancySystem::Calculate(*water, component, {}, 10.0F);
    const BuoyancyPose pitched{
        .worldPositionMeters = {}, .worldOrientation = {0.0F, 0.0F, 0.2588190451F, 0.9659258263F}};
    const auto rotated = BuoyancySystem::Calculate(*water, component, pitched, 10.0F);
    if (!level || !rotated)
    {
        return false;
    }
    return E2Near(level->points[0].submergedFraction, 0.5F) &&
           E2Near(level->points[1].submergedFraction, 0.5F) &&
           E2Near(rotated->points[0].worldPositionMeters.y, 1.0F) &&
           E2Near(rotated->points[1].worldPositionMeters.y, -1.0F) &&
           E2Near(rotated->points[0].signedDepthMeters, -1.0F) &&
           E2Near(rotated->points[1].signedDepthMeters, 1.0F) &&
           E2Near(rotated->points[0].submergedFraction, 0.25F) &&
           E2Near(rotated->points[1].submergedFraction, 0.75F) &&
           rotated->points[0].forceNewtons.y < rotated->points[1].forceNewtons.y;
}

bool BuoyancyNeutralDisplacementIdentity()
{
    constexpr float Density = 1000.0F;
    constexpr float Mass = 2000.0F;
    constexpr float Gravity = 10.0F;
    const auto water = DeepRun::Marine::WaterBody::Create(
        {.surfaceLevelY = 0.0F, .densityKgPerCubicMeter = Density});
    const BuoyancyComponent component{
        .points = {E2Point({2.0F, 0.0F, 0.0F}, 0.75F, 1.0F),
                   E2Point({-2.0F, 0.0F, 0.0F}, Mass / Density - 0.75F, 1.0F)}};
    const BuoyancyPose pose{.worldPositionMeters = {0.0F, -3.0F, 0.0F}};
    const auto result = BuoyancySystem::Calculate(*water, component, pose, Gravity);
    return result && E2Near(result->totalSubmergedVolumeCubicMeters, Mass / Density) &&
           E2Near(result->totalForceNewtons.x, 0.0F) && E2Near(result->totalForceNewtons.y, Mass * Gravity) &&
           E2Near(result->totalForceNewtons.z, 0.0F);
}

bool BuoyancyTotalsSumPublishedPointResults()
{
    // These deliberately awkward values make the former sum-of-unrounded-intermediates path differ by one
    // float ULP from the sum of the published point results for both volume and force.
    const auto water = DeepRun::Marine::WaterBody::Create(
        {.surfaceLevelY = 0.0F, .densityKgPerCubicMeter = 997.3F});
    if (!water)
    {
        return false;
    }
    const BuoyancyComponent component{
        .points = {E2Point({0.0F, -0.597106695F, 0.0F}, 1.83203733F, 4.6845293F),
                   E2Point({0.0F, -1.00079024F, 0.0F}, 0.930839837F, 3.84205127F),
                   E2Point({0.0F, -0.316459775F, 0.0F}, 0.407760501F, 1.8995986F)}};
    const auto result = BuoyancySystem::Calculate(*water, component, {}, 9.8137F);
    if (!result || result->points.size() != component.points.size())
    {
        return false;
    }

    double publishedForceX = 0.0;
    double publishedForceY = 0.0;
    double publishedForceZ = 0.0;
    double publishedVolume = 0.0;
    for (const auto& point : result->points)
    {
        publishedForceX += static_cast<double>(point.forceNewtons.x);
        publishedForceY += static_cast<double>(point.forceNewtons.y);
        publishedForceZ += static_cast<double>(point.forceNewtons.z);
        publishedVolume += static_cast<double>(point.submergedVolumeCubicMeters);
    }

    const PhysicsVector3 expectedForce{
        static_cast<float>(publishedForceX),
        static_cast<float>(publishedForceY),
        static_cast<float>(publishedForceZ)};
    const float expectedVolume = static_cast<float>(publishedVolume);
    const auto withinQuarterUlp = [](const float actual, const float expected) {
        const float adjacent = std::nextafter(expected, std::numeric_limits<float>::infinity());
        const float quarterUlp = std::abs(adjacent - expected) * 0.25F;
        return std::abs(actual - expected) <= quarterUlp;
    };

    return withinQuarterUlp(result->totalForceNewtons.x, expectedForce.x) &&
           withinQuarterUlp(result->totalForceNewtons.y, expectedForce.y) &&
           withinQuarterUlp(result->totalForceNewtons.z, expectedForce.z) &&
           withinQuarterUlp(result->totalSubmergedVolumeCubicMeters, expectedVolume);
}

bool BuoyancyRejectsDerivedOverflow()
{
    const float maximum = std::numeric_limits<float>::max();
    const BuoyancyComponent onePoint{.points = {E2Point({}, 1.0F, 1.0F)}};
    const BuoyancyPose submerged{.worldPositionMeters = {0.0F, -2.0F, 0.0F}};

    const auto perPointWater = DeepRun::Marine::WaterBody::Create(
        {.surfaceLevelY = 0.0F, .densityKgPerCubicMeter = maximum});
    const auto perPoint = BuoyancySystem::Calculate(*perPointWater, onePoint, submerged, 2.0F);
    if (!E2HasError(perPoint, BuoyancyErrorCode::NonFiniteResult))
    {
        return false;
    }

    const auto totalWater = DeepRun::Marine::WaterBody::Create(
        {.surfaceLevelY = 0.0F, .densityKgPerCubicMeter = maximum / 4.0F});
    const BuoyancyComponent threePoints{
        .points = {E2Point({-1.0F, 0.0F, 0.0F}), E2Point({0.0F, 0.0F, 0.0F}),
                   E2Point({1.0F, 0.0F, 0.0F})}};
    const auto total = BuoyancySystem::Calculate(*totalWater, threePoints, submerged, 2.0F);
    if (!E2HasError(total, BuoyancyErrorCode::NonFiniteResult))
    {
        return false;
    }

    // WaterBody permits any finite surface and point position, so their float subtraction can overflow.
    // E2 must reject the resulting signed-depth output instead of returning it as infinity.
    const auto depthWater = DeepRun::Marine::WaterBody::Create(
        {.surfaceLevelY = maximum, .densityKgPerCubicMeter = 1000.0F});
    const BuoyancyPose extremePose{.worldPositionMeters = {0.0F, -maximum, 0.0F}};
    return E2HasError(BuoyancySystem::Calculate(*depthWater, onePoint, extremePose, 10.0F),
                      BuoyancyErrorCode::NonFiniteResult);
}

// ---------------------------------------------------------------------------
// M2 Slice E3: generic fixed-phase ordering, authoritative gravity, and headless point-force integration.
// ---------------------------------------------------------------------------

namespace
{
using DeepRun::Marine::WaterBody;

constexpr float E3FixedDeltaSeconds = 1.0F / 60.0F;

BuoyancyComponent E3NeutralBuoyancy(
    const float massKg,
    const float densityKgPerCubicMeter,
    const float localYMeters = 0.0F)
{
    const float pointVolume = (massKg / densityKgPerCubicMeter) / 4.0F;
    return BuoyancyComponent{
        .points = {E2Point({3.0F, localYMeters, 0.0F}, pointVolume, 1.0F),
                   E2Point({1.0F, localYMeters, 0.0F}, pointVolume, 1.0F),
                   E2Point({-1.0F, localYMeters, 0.0F}, pointVolume, 1.0F),
                   E2Point({-3.0F, localYMeters, 0.0F}, pointVolume, 1.0F)}};
}

DeepRun::Physics::DynamicBoxBodyCreateInfo E3BodyInfo(
    const float massKg,
    const PhysicsVector3 initialVelocity = {},
    const PhysicsQuaternion orientation = {})
{
    DeepRun::Physics::DynamicBoxBodyCreateInfo info;
    info.halfExtents = {2.0F, 1.0F, 1.0F};
    info.mass = massKg;
    info.position = {0.0F, -20.0F, 0.0F};
    info.orientation = orientation;
    info.gravityEnabled = true;
    info.linearDamping = 0.0F;
    info.angularDamping = 0.0F;
    info.initialLinearVelocity = initialVelocity;
    info.initialAngularVelocity = {};
    return info;
}

bool E3ApplyBuoyancyAndStep(
    DeepRun::Physics::PhysicsWorld& world,
    const DeepRun::Physics::PhysicsBodyHandle handle,
    const WaterBody& water,
    const BuoyancyComponent& buoyancy,
    const float fixedDeltaSeconds = E3FixedDeltaSeconds)
{
    const auto state = world.GetBodyState(handle);
    const auto gravity = world.Gravity();
    if (!state || !gravity || !gravity->IsFinite())
    {
        return false;
    }
    const double gravityMagnitude = std::sqrt(
        static_cast<double>(gravity->x) * gravity->x + static_cast<double>(gravity->y) * gravity->y +
        static_cast<double>(gravity->z) * gravity->z);
    if (!std::isfinite(gravityMagnitude) || gravityMagnitude <= 0.0 ||
        gravityMagnitude > (std::numeric_limits<float>::max)())
    {
        return false;
    }

    const auto result = BuoyancySystem::Calculate(
        water,
        buoyancy,
        BuoyancyPose{
            .worldPositionMeters = state->position,
            .worldOrientation = state->orientation},
        static_cast<float>(gravityMagnitude));
    if (!result)
    {
        return false;
    }

    for (const auto& point : result->points)
    {
        if (!world.AddForceAtWorldPosition(handle, point.forceNewtons, point.worldPositionMeters))
        {
            return false;
        }
    }
    world.Step(fixedDeltaSeconds);
    return true;
}

bool E3WriteHeadlessEngineConfig(const std::filesystem::path& path)
{
    WriteFile(
        path,
        R"({"renderer":{"vsync":true,"width":800,"height":600},"physics":{"fixedHz":60}})");
    return std::filesystem::exists(path);
}
} // namespace

bool PhysicsGravityQueryContract()
{
    DeepRun::Diagnostics::Logger logger;
    DeepRun::Physics::PhysicsWorld world(logger);
    if (world.Gravity().has_value() || !world.Initialize())
    {
        return false;
    }
    const auto gravity = world.Gravity();
    return gravity && gravity->IsFinite() && std::abs(gravity->x) < 1.0e-6F &&
           std::abs(gravity->y + 9.81F) < 1.0e-4F && std::abs(gravity->z) < 1.0e-6F;
}

bool FixedUpdateHookRunsBeforePhysicsStep()
{
    TemporaryDirectory temporary;
    const std::filesystem::path configPath = temporary.Path() / "engine.json";
    if (!E3WriteHeadlessEngineConfig(configPath))
    {
        return false;
    }

    DeepRun::Core::Engine engine({
        .headless = true,
        .configPath = configPath,
        .contentRoot = temporary.Path() / "Content"});
    if (!engine.Initialize() || engine.Physics() == nullptr)
    {
        return false;
    }

    auto info = E3BodyInfo(1.0F);
    info.position = {};
    info.gravityEnabled = false;
    const auto handle = engine.Physics()->CreateDynamicBoxBody(info);
    const auto initial = engine.Physics()->GetBodyState(handle);
    int invocationCount = 0;
    float observedFixedDelta = 0.0F;
    const auto wave = WaterBody::Create({.surfaceLevelY = 0.0F, .densityKgPerCubicMeter = 1025.0F,
                                        .waves = DeepRun::Marine::M3WaterWaveField});
    if (!wave || engine.SimulationTimeSeconds() != 0.0) return false;
    const auto beforeRender = wave->SampleWaveSurface({37.5F, 0.0F, 0.0F}, engine.SimulationTimeSeconds());
    static_cast<void>(engine.Render()); // Headless render cannot advance fixed-step authority.
    const auto afterRender = wave->SampleWaveSurface({37.5F, 0.0F, 0.0F}, engine.SimulationTimeSeconds());
    if (!beforeRender || !afterRender || beforeRender->surfaceLevelY != afterRender->surfaceLevelY ||
        engine.SimulationTimeSeconds() != 0.0) return false;
    const bool updateReturned = engine.Update([&](const float fixedDeltaSeconds) {
        ++invocationCount;
        observedFixedDelta = fixedDeltaSeconds;
        const auto beforeStep = engine.Physics()->GetBodyState(handle);
        return engine.SimulationTimeSeconds() == 0.0 && beforeStep && beforeStep->linearVelocity.x == 0.0F &&
               engine.Physics()->AddForceAtWorldPosition(
                   handle, {60.0F, 0.0F, 0.0F}, beforeStep->position);
    });
    const auto after = engine.Physics()->GetBodyState(handle);
    const auto advancedWave = wave->SampleWaveSurface({37.5F, 0.0F, 0.0F}, engine.SimulationTimeSeconds());
    const bool passed = initial && after && !updateReturned && invocationCount == 1 &&
                        engine.SimulationTimeSeconds() == static_cast<double>(observedFixedDelta) &&
                        advancedWave && advancedWave->surfaceLevelY != beforeRender->surfaceLevelY &&
                        std::abs(observedFixedDelta - E3FixedDeltaSeconds) < 1.0e-6F &&
                        after->linearVelocity.x > 0.5F && after->position.x > initial->position.x &&
                        engine.ExitCode() == 0 &&
                        engine.Lifecycle() == DeepRun::Core::EngineLifecycle::ShutdownRequested;
    engine.Shutdown();
    return passed;
}

bool FixedUpdateHookFailurePreventsPhysicsStep()
{
    TemporaryDirectory temporary;
    const std::filesystem::path configPath = temporary.Path() / "engine.json";
    if (!E3WriteHeadlessEngineConfig(configPath))
    {
        return false;
    }

    DeepRun::Core::Engine engine({
        .headless = true,
        .configPath = configPath,
        .contentRoot = temporary.Path() / "Content"});
    if (!engine.Initialize() || engine.Physics() == nullptr)
    {
        return false;
    }

    const auto handle = engine.Physics()->CreateDynamicBoxBody(E3BodyInfo(10.0F));
    const auto initial = engine.Physics()->GetBodyState(handle);
    int invocationCount = 0;
    const bool updateReturned = engine.Update([&](const float) {
        ++invocationCount;
        return false;
    });
    const auto after = engine.Physics()->GetBodyState(handle);
    const bool unchanged = initial && after && after->position == initial->position &&
                           after->linearVelocity == initial->linearVelocity &&
                           after->orientation == initial->orientation &&
                           after->angularVelocity == initial->angularVelocity;
    const bool passed = !updateReturned && invocationCount == 1 && unchanged && engine.ExitCode() == 11 &&
                        engine.SimulationTimeSeconds() == 0.0 &&
                        engine.Lifecycle() == DeepRun::Core::EngineLifecycle::ShutdownRequested;
    engine.Shutdown();
    return passed;
}

bool BuoyancyIntegrationNeutralRest()
{
    constexpr float Mass = 2000.0F;
    constexpr float Density = 1000.0F;
    constexpr int Steps = 300; // five seconds
    DeepRun::Diagnostics::Logger logger;
    DeepRun::Physics::PhysicsWorld world(logger);
    const auto water = WaterBody::Create(
        {.surfaceLevelY = 0.0F, .densityKgPerCubicMeter = Density});
    if (!world.Initialize() || !water)
    {
        return false;
    }
    const auto handle = world.CreateDynamicBoxBody(E3BodyInfo(Mass));
    const auto initial = world.GetBodyState(handle);
    if (!handle.IsValid() || !initial)
    {
        return false;
    }

    const BuoyancyComponent buoyancy = E3NeutralBuoyancy(Mass, Density);
    for (int step = 0; step < Steps; ++step)
    {
        if (!E3ApplyBuoyancyAndStep(world, handle, *water, buoyancy))
        {
            return false;
        }
    }
    const auto after = world.GetBodyState(handle);
    if (!after)
    {
        return false;
    }
    std::cout << "[E3 evidence] neutral rest: Y " << initial->position.y << " -> " << after->position.y
              << ", Vy " << after->linearVelocity.y << ", omegaZ " << after->angularVelocity.z << '\n';
    return StateIsFinite(*after) && std::abs(after->position.y - initial->position.y) < 0.5F &&
           std::abs(after->linearVelocity.y) < 0.1F &&
           std::abs(after->position.x - initial->position.x) < 1.0e-3F &&
           std::abs(after->position.z - initial->position.z) < 1.0e-3F &&
           std::abs(after->angularVelocity.x) < 1.0e-3F &&
           std::abs(after->angularVelocity.y) < 1.0e-3F &&
           std::abs(after->angularVelocity.z) < 1.0e-3F &&
           DeepRun::Physics::PhysicsQuaternion::SameRotation(after->orientation, initial->orientation);
}

bool BuoyancyIntegrationPreservesVerticalVelocity()
{
    constexpr float Mass = 2000.0F;
    constexpr float Density = 1000.0F;
    constexpr float InitialVelocityY = -3.0F;
    constexpr int Steps = 120; // two seconds
    DeepRun::Diagnostics::Logger logger;
    DeepRun::Physics::PhysicsWorld world(logger);
    const auto water = WaterBody::Create(
        {.surfaceLevelY = 0.0F, .densityKgPerCubicMeter = Density});
    if (!world.Initialize() || !water)
    {
        return false;
    }
    const auto handle = world.CreateDynamicBoxBody(
        E3BodyInfo(Mass, {0.0F, InitialVelocityY, 0.0F}));
    const auto initial = world.GetBodyState(handle);
    if (!handle.IsValid() || !initial)
    {
        return false;
    }

    const BuoyancyComponent buoyancy = E3NeutralBuoyancy(Mass, Density);
    for (int step = 0; step < Steps; ++step)
    {
        if (!E3ApplyBuoyancyAndStep(world, handle, *water, buoyancy))
        {
            return false;
        }
    }
    const auto after = world.GetBodyState(handle);
    if (!after)
    {
        return false;
    }
    const float expectedY = initial->position.y + InitialVelocityY * (Steps * E3FixedDeltaSeconds);
    std::cout << "[E3 evidence] no drag: Y " << initial->position.y << " -> " << after->position.y
              << ", expected " << expectedY << ", Vy " << initial->linearVelocity.y << " -> "
              << after->linearVelocity.y << '\n';
    return StateIsFinite(*after) && std::abs(after->linearVelocity.y - InitialVelocityY) < 0.1F &&
           std::abs(after->position.y - expectedY) < 0.1F &&
           std::abs(after->position.x - initial->position.x) < 1.0e-3F &&
           std::abs(after->position.z - initial->position.z) < 1.0e-3F;
}

bool BuoyancyPointForcesCreateRestoringPitch()
{
    constexpr float Mass = 2000.0F;
    constexpr float Density = 1000.0F;
    constexpr float HalfAngleRadians = 2.5F * (3.14159265358979323846F / 180.0F);
    DeepRun::Diagnostics::Logger logger;
    DeepRun::Physics::PhysicsWorld world(logger);
    const auto water = WaterBody::Create(
        {.surfaceLevelY = 0.0F, .densityKgPerCubicMeter = Density});
    if (!world.Initialize() || !water)
    {
        return false;
    }

    auto info = E3BodyInfo(
        Mass,
        {},
        {0.0F, 0.0F, std::sin(HalfAngleRadians), std::cos(HalfAngleRadians)});
    DeepRun::Physics::PhysicsDegreesOfFreedom planar;
    planar.translationX = true;
    planar.translationY = true;
    planar.translationZ = false;
    planar.rotationX = false;
    planar.rotationY = false;
    planar.rotationZ = true;
    info.degreesOfFreedom = planar;

    const auto handle = world.CreateDynamicBoxBody(info);
    const auto initial = world.GetBodyState(handle);
    if (!handle.IsValid() || !initial ||
        !E3ApplyBuoyancyAndStep(world, handle, *water, E3NeutralBuoyancy(Mass, Density, 1.0F)))
    {
        return false;
    }
    const auto after = world.GetBodyState(handle);
    if (!after)
    {
        return false;
    }
    std::cout << "[E3 evidence] restoring pitch: omegaZ " << initial->angularVelocity.z << " -> "
              << after->angularVelocity.z << ", locked omegaX/Y " << after->angularVelocity.x << "/"
              << after->angularVelocity.y << '\n';
    return StateIsFinite(*after) && after->angularVelocity.z < -1.0e-5F &&
           std::abs(after->position.z - initial->position.z) < 1.0e-5F &&
           std::abs(after->linearVelocity.z) < 1.0e-5F &&
           std::abs(after->angularVelocity.x) < 1.0e-5F &&
           std::abs(after->angularVelocity.y) < 1.0e-5F;
}

// ---------------------------------------------------------------------------
// M2 Slice F1: pure fully-immersed directional hydrodynamic drag. No body, world, time step, or runtime
// integration participates in these tests.
// ---------------------------------------------------------------------------

namespace
{
using DeepRun::Marine::HydroDragComponent;
using DeepRun::Marine::HydroDragError;
using DeepRun::Marine::HydroDragErrorCode;
using DeepRun::Marine::HydroDragResult;
using DeepRun::Marine::HydroDragState;
using DeepRun::Marine::HydroDragSystem;

HydroDragComponent F1Component(
    const PhysicsVector3 linear = {},
    const PhysicsVector3 angular = {})
{
    return HydroDragComponent{
        .linearEffectiveAreaSquareMeters = linear,
        .angularEffectiveMomentMeters5 = angular};
}

HydroDragState F1State(
    const PhysicsVector3 linearVelocity = {},
    const PhysicsVector3 angularVelocity = {},
    const PhysicsQuaternion orientation = {})
{
    return HydroDragState{
        .worldOrientation = orientation,
        .worldLinearVelocityMetersPerSecond = linearVelocity,
        .worldAngularVelocityRadiansPerSecond = angularVelocity};
}

bool F1HasError(
    const std::expected<HydroDragResult, HydroDragError>& result,
    const HydroDragErrorCode code)
{
    return !result && result.error().code == code && !result.error().message.empty();
}

std::expected<DeepRun::Marine::WaterBody, DeepRun::Marine::WaterBodyError> F1Water(const float density)
{
    return DeepRun::Marine::WaterBody::Create(
        {.surfaceLevelY = 100.0F, .densityKgPerCubicMeter = density});
}
} // namespace

bool HydroDragZeroVelocityIsExactZero()
{
    const auto water = F1Water(997.0F);
    const auto result = HydroDragSystem::Calculate(
        *water, F1Component({2.0F, 3.0F, 4.0F}, {5.0F, 6.0F, 7.0F}), F1State());
    return result && result->forceNewtons == PhysicsVector3{} &&
           result->torqueNewtonMeters == PhysicsVector3{};
}

bool HydroDragLinearSignReversal()
{
    const auto water = F1Water(1000.0F);
    const HydroDragComponent component = F1Component({2.0F, 0.0F, 0.0F});
    const auto positive = HydroDragSystem::Calculate(*water, component, F1State({2.0F, 0.0F, 0.0F}));
    const auto negative = HydroDragSystem::Calculate(*water, component, F1State({-2.0F, 0.0F, 0.0F}));
    return positive && negative && E2VectorNear(positive->forceNewtons, {-4000.0F, 0.0F, 0.0F}) &&
           E2VectorNear(negative->forceNewtons, {4000.0F, 0.0F, 0.0F}) &&
           E2Near(std::abs(positive->forceNewtons.x), std::abs(negative->forceNewtons.x)) &&
           positive->torqueNewtonMeters == PhysicsVector3{} && negative->torqueNewtonMeters == PhysicsVector3{};
}

bool HydroDragLinearQuadraticSpeedScaling()
{
    const auto water = F1Water(1000.0F);
    const HydroDragComponent component = F1Component({1.25F, 0.0F, 0.0F});
    const auto speedTwo = HydroDragSystem::Calculate(*water, component, F1State({2.0F, 0.0F, 0.0F}));
    const auto speedFour = HydroDragSystem::Calculate(*water, component, F1State({4.0F, 0.0F, 0.0F}));
    return speedTwo && speedFour && speedTwo->forceNewtons.x < 0.0F &&
           E2Near(std::abs(speedFour->forceNewtons.x) / std::abs(speedTwo->forceNewtons.x), 4.0F);
}

bool HydroDragUsesWaterDensityScaling()
{
    const auto water = F1Water(500.0F);
    const auto denseWater = F1Water(1000.0F);
    const HydroDragComponent component = F1Component({2.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 3.0F});
    const HydroDragState state = F1State({3.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 2.0F});
    const auto first = HydroDragSystem::Calculate(*water, component, state);
    const auto second = HydroDragSystem::Calculate(*denseWater, component, state);
    return first && second &&
           E2Near(std::abs(second->forceNewtons.x) / std::abs(first->forceNewtons.x), 2.0F) &&
           E2Near(std::abs(second->torqueNewtonMeters.z) / std::abs(first->torqueNewtonMeters.z), 2.0F);
}

bool HydroDragDirectionalAnisotropy()
{
    const auto water = F1Water(1000.0F);
    const HydroDragComponent component = F1Component({2.0F, 8.0F, 0.0F});
    const auto alongX = HydroDragSystem::Calculate(*water, component, F1State({3.0F, 0.0F, 0.0F}));
    const auto alongY = HydroDragSystem::Calculate(*water, component, F1State({0.0F, 3.0F, 0.0F}));
    return alongX && alongY &&
           E2Near(std::abs(alongY->forceNewtons.y) / std::abs(alongX->forceNewtons.x), 4.0F) &&
           E2Near(alongX->forceNewtons.y, 0.0F) && E2Near(alongY->forceNewtons.x, 0.0F);
}

bool HydroDragLinearBodyOrientation()
{
    const auto water = F1Water(1000.0F);
    const HydroDragComponent component = F1Component({2.0F, 0.0F, 0.0F});
    // Scaled +90 degree Z quaternion also proves finite non-unit inputs are normalized internally.
    const auto result = HydroDragSystem::Calculate(
        *water, component, F1State({0.0F, 3.0F, 0.0F}, {}, {0.0F, 0.0F, 2.0F, 2.0F}));
    return result && E2VectorNear(result->forceNewtons, {0.0F, -9000.0F, 0.0F}) &&
           result->torqueNewtonMeters == PhysicsVector3{};
}

bool HydroDragQuaternionSignEquivalence()
{
    const auto water = F1Water(1025.0F);
    const HydroDragComponent component = F1Component({1.0F, 2.0F, 3.0F}, {4.0F, 5.0F, 6.0F});
    const PhysicsQuaternion q{0.2F, -0.3F, 0.4F, 0.5F};
    const PhysicsQuaternion negativeQ{-q.x, -q.y, -q.z, -q.w};
    const auto first = HydroDragSystem::Calculate(
        *water, component, F1State({3.0F, -2.0F, 1.0F}, {-0.5F, 0.75F, 1.25F}, q));
    const auto second = HydroDragSystem::Calculate(
        *water, component, F1State({3.0F, -2.0F, 1.0F}, {-0.5F, 0.75F, 1.25F}, negativeQ));
    return first && second && E2VectorNear(first->forceNewtons, second->forceNewtons) &&
           E2VectorNear(first->torqueNewtonMeters, second->torqueNewtonMeters);
}

bool HydroDragAngularSignReversal()
{
    const auto water = F1Water(1000.0F);
    const HydroDragComponent component = F1Component({}, {0.0F, 0.0F, 3.0F});
    const auto positive = HydroDragSystem::Calculate(*water, component, F1State({}, {0.0F, 0.0F, 1.0F}));
    const auto negative = HydroDragSystem::Calculate(*water, component, F1State({}, {0.0F, 0.0F, -1.0F}));
    return positive && negative && E2VectorNear(positive->torqueNewtonMeters, {0.0F, 0.0F, -1500.0F}) &&
           E2VectorNear(negative->torqueNewtonMeters, {0.0F, 0.0F, 1500.0F}) &&
           positive->forceNewtons == PhysicsVector3{} && negative->forceNewtons == PhysicsVector3{};
}

bool HydroDragAngularQuadraticSpeedScaling()
{
    const auto water = F1Water(1000.0F);
    const HydroDragComponent component = F1Component({}, {0.0F, 0.0F, 2.0F});
    const auto speedOne = HydroDragSystem::Calculate(*water, component, F1State({}, {0.0F, 0.0F, 1.0F}));
    const auto speedTwo = HydroDragSystem::Calculate(*water, component, F1State({}, {0.0F, 0.0F, 2.0F}));
    return speedOne && speedTwo && speedOne->torqueNewtonMeters.z < 0.0F &&
           E2Near(std::abs(speedTwo->torqueNewtonMeters.z) / std::abs(speedOne->torqueNewtonMeters.z), 4.0F);
}

bool HydroDragAngularBodyOrientation()
{
    const auto water = F1Water(1000.0F);
    const HydroDragComponent component = F1Component({}, {4.0F, 0.0F, 0.0F});
    const auto result = HydroDragSystem::Calculate(
        *water, component, F1State({}, {0.0F, 2.0F, 0.0F}, {0.0F, 0.0F, 2.0F, 2.0F}));
    return result && E2VectorNear(result->torqueNewtonMeters, {0.0F, -8000.0F, 0.0F}) &&
           result->forceNewtons == PhysicsVector3{};
}

bool HydroDragLinearAngularIndependence()
{
    const auto water = F1Water(1000.0F);
    const HydroDragComponent component = F1Component({2.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 3.0F});
    const auto linearOnly = HydroDragSystem::Calculate(*water, component, F1State({2.0F, 0.0F, 0.0F}));
    const auto angularOnly = HydroDragSystem::Calculate(*water, component, F1State({}, {0.0F, 0.0F, 2.0F}));
    return linearOnly && angularOnly && linearOnly->forceNewtons.x != 0.0F &&
           linearOnly->torqueNewtonMeters == PhysicsVector3{} &&
           angularOnly->forceNewtons == PhysicsVector3{} && angularOnly->torqueNewtonMeters.z != 0.0F;
}

bool HydroDragZeroCoefficientsAreValid()
{
    const auto water = F1Water(1000.0F);
    const auto result = HydroDragSystem::Calculate(
        *water, F1Component(), F1State({10.0F, -20.0F, 30.0F}, {-1.0F, 2.0F, -3.0F}));
    return result && result->forceNewtons == PhysicsVector3{} &&
           result->torqueNewtonMeters == PhysicsVector3{};
}

bool HydroDragRejectsInvalidConfiguration()
{
    const auto water = F1Water(1000.0F);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    for (const float invalid : {-1.0F, nan, infinity})
    {
        if (!F1HasError(
                HydroDragSystem::Calculate(*water, F1Component({invalid, 1.0F, 1.0F}), F1State()),
                HydroDragErrorCode::InvalidConfiguration) ||
            !F1HasError(
                HydroDragSystem::Calculate(*water, F1Component({}, {1.0F, invalid, 1.0F}), F1State()),
                HydroDragErrorCode::InvalidConfiguration))
        {
            return false;
        }
    }
    return true;
}

bool HydroDragRejectsInvalidState()
{
    const auto water = F1Water(1000.0F);
    const HydroDragComponent component = F1Component({1.0F, 1.0F, 1.0F}, {1.0F, 1.0F, 1.0F});
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    const HydroDragState invalidStates[] = {
        F1State({nan, 0.0F, 0.0F}),
        F1State({0.0F, infinity, 0.0F}),
        F1State({}, {0.0F, nan, 0.0F}),
        F1State({}, {0.0F, 0.0F, -infinity}),
        F1State({}, {}, {0.0F, 0.0F, 0.0F, 0.0F}),
        F1State({}, {}, {nan, 0.0F, 0.0F, 1.0F}),
        F1State({}, {}, {0.0F, infinity, 0.0F, 1.0F})};
    for (const HydroDragState& state : invalidStates)
    {
        if (!F1HasError(HydroDragSystem::Calculate(*water, component, state), HydroDragErrorCode::InvalidState))
        {
            return false;
        }
    }
    return true;
}

bool HydroDragRejectsDerivedOverflow()
{
    const float maximum = (std::numeric_limits<float>::max)();
    const auto water = F1Water(maximum);
    const auto forceOverflow = HydroDragSystem::Calculate(
        *water, F1Component({maximum, 0.0F, 0.0F}), F1State({2.0F, 0.0F, 0.0F}));
    const auto torqueOverflow = HydroDragSystem::Calculate(
        *water, F1Component({}, {0.0F, 0.0F, maximum}), F1State({}, {0.0F, 0.0F, 2.0F}));
    return F1HasError(forceOverflow, HydroDragErrorCode::NonFiniteResult) &&
           F1HasError(torqueOverflow, HydroDragErrorCode::NonFiniteResult);
}

// ---------------------------------------------------------------------------
// M2 Slice H1: pure fully-immersed diving-plane force and application-point calculation. No rigid body,
// force application, time step, actuator, player command, propulsion, or presentation participates.
// ---------------------------------------------------------------------------

namespace
{
using DeepRun::Marine::ControlSurfaceComponent;
using DeepRun::Marine::ControlSurfaceError;
using DeepRun::Marine::ControlSurfaceErrorCode;
using DeepRun::Marine::ControlSurfaceKinematics;
using DeepRun::Marine::ControlSurfaceResult;
using DeepRun::Marine::ControlSurfaceSystem;

ControlSurfaceComponent H1Component(
    const PhysicsVector3 position = {-30.0F, 0.0F, 0.0F},
    const float effectiveArea = 10.0F)
{
    return {
        .bodyLocalPositionMeters = position,
        .maxEffectiveLiftAreaSquareMeters = effectiveArea};
}

ControlSurfaceKinematics H1Kinematics(
    const PhysicsVector3 velocity = {},
    const PhysicsQuaternion orientation = {},
    const PhysicsVector3 bodyPosition = {10.0F, 20.0F, 0.0F})
{
    return {
        .bodyWorldPositionMeters = bodyPosition,
        .worldOrientation = orientation,
        .worldLinearVelocityMetersPerSecond = velocity};
}

bool H1HasError(
    const std::expected<ControlSurfaceResult, ControlSurfaceError>& result,
    const ControlSurfaceErrorCode code)
{
    return !result && result.error().code == code && !result.error().message.empty();
}
} // namespace

bool ControlSurfaceZeroDeflection()
{
    const auto water = F1Water(1000.0F);
    const auto result = ControlSurfaceSystem::Calculate(
        *water, H1Component(), H1Kinematics({5.0F, 0.0F, 0.0F}), 0.0F);
    return result && result->worldPositionMeters == PhysicsVector3{-20.0F, 20.0F, 0.0F} &&
           result->bodyForwardSpeedMetersPerSecond == 5.0F && result->deflectionFraction == 0.0F &&
           result->forceNewtons == PhysicsVector3{};
}

bool ControlSurfaceZeroSpeedHasNoAuthority()
{
    const auto water = F1Water(1000.0F);
    const auto result = ControlSurfaceSystem::Calculate(*water, H1Component(), H1Kinematics(), 1.0F);
    return result && result->bodyForwardSpeedMetersPerSecond == 0.0F &&
           result->forceNewtons == PhysicsVector3{};
}

bool ControlSurfaceAheadPositiveForce()
{
    const auto water = F1Water(1000.0F);
    const auto result = ControlSurfaceSystem::Calculate(
        *water, H1Component(), H1Kinematics({2.0F, 0.0F, 0.0F}), 0.5F);
    return result && E2Near(result->bodyForwardSpeedMetersPerSecond, 2.0F) &&
           E2VectorNear(result->forceNewtons, {0.0F, 10'000.0F, 0.0F});
}

bool ControlSurfaceDeflectionSign()
{
    const auto water = F1Water(1000.0F);
    const ControlSurfaceKinematics kinematics = H1Kinematics({2.0F, 0.0F, 0.0F});
    const auto positive = ControlSurfaceSystem::Calculate(*water, H1Component(), kinematics, 0.5F);
    const auto negative = ControlSurfaceSystem::Calculate(*water, H1Component(), kinematics, -0.5F);
    return positive && negative && positive->forceNewtons.y > 0.0F && negative->forceNewtons.y < 0.0F &&
           E2Near(positive->forceNewtons.y, -negative->forceNewtons.y);
}

bool ControlSurfaceReverseFlowReversesForce()
{
    const auto water = F1Water(1000.0F);
    const auto ahead = ControlSurfaceSystem::Calculate(
        *water, H1Component(), H1Kinematics({2.0F, 0.0F, 0.0F}), 0.5F);
    const auto astern = ControlSurfaceSystem::Calculate(
        *water, H1Component(), H1Kinematics({-2.0F, 0.0F, 0.0F}), 0.5F);
    return ahead && astern && ahead->bodyForwardSpeedMetersPerSecond == 2.0F &&
           astern->bodyForwardSpeedMetersPerSecond == -2.0F && ahead->forceNewtons.y > 0.0F &&
           astern->forceNewtons.y < 0.0F && E2Near(ahead->forceNewtons.y, -astern->forceNewtons.y);
}

bool ControlSurfaceQuadraticSpeedScaling()
{
    const auto water = F1Water(1000.0F);
    const auto speedTwo = ControlSurfaceSystem::Calculate(
        *water, H1Component(), H1Kinematics({2.0F, 0.0F, 0.0F}), 1.0F);
    const auto speedFour = ControlSurfaceSystem::Calculate(
        *water, H1Component(), H1Kinematics({4.0F, 0.0F, 0.0F}), 1.0F);
    return speedTwo && speedFour && E2Near(speedFour->forceNewtons.y / speedTwo->forceNewtons.y, 4.0F);
}

bool ControlSurfaceLinearDeflectionScaling()
{
    const auto water = F1Water(1000.0F);
    const ControlSurfaceKinematics kinematics = H1Kinematics({3.0F, 0.0F, 0.0F});
    const auto quarter = ControlSurfaceSystem::Calculate(*water, H1Component(), kinematics, 0.25F);
    const auto full = ControlSurfaceSystem::Calculate(*water, H1Component(), kinematics, 1.0F);
    return quarter && full && E2Near(full->forceNewtons.y / quarter->forceNewtons.y, 4.0F);
}

bool ControlSurfaceUsesWaterDensity()
{
    const auto water = F1Water(500.0F);
    const auto denseWater = F1Water(1000.0F);
    const ControlSurfaceKinematics kinematics = H1Kinematics({3.0F, 0.0F, 0.0F});
    const auto first = ControlSurfaceSystem::Calculate(*water, H1Component(), kinematics, 0.75F);
    const auto second = ControlSurfaceSystem::Calculate(*denseWater, H1Component(), kinematics, 0.75F);
    return first && second && E2Near(second->forceNewtons.y / first->forceNewtons.y, 2.0F);
}

bool ControlSurfaceBodyOrientation()
{
    constexpr float HalfSqrtTwo = 0.7071067811865475F;
    const auto water = F1Water(1000.0F);
    const auto result = ControlSurfaceSystem::Calculate(
        *water,
        H1Component(),
        H1Kinematics({0.0F, 2.0F, 0.0F}, {0.0F, 0.0F, HalfSqrtTwo, HalfSqrtTwo}),
        0.5F);
    return result && E2Near(result->bodyForwardSpeedMetersPerSecond, 2.0F) &&
           E2VectorNear(result->forceNewtons, {-10'000.0F, 0.0F, 0.0F});
}

bool ControlSurfaceApplicationPointRotation()
{
    constexpr float HalfSqrtTwo = 0.7071067811865475F;
    const auto water = F1Water(1000.0F);
    const auto identity = ControlSurfaceSystem::Calculate(
        *water, H1Component(), H1Kinematics({2.0F, 0.0F, 0.0F}), 0.5F);
    const auto rotated = ControlSurfaceSystem::Calculate(
        *water,
        H1Component(),
        H1Kinematics({0.0F, 2.0F, 0.0F}, {0.0F, 0.0F, HalfSqrtTwo, HalfSqrtTwo}),
        0.5F);
    return identity && rotated && E2VectorNear(identity->worldPositionMeters, {-20.0F, 20.0F, 0.0F}) &&
           E2VectorNear(rotated->worldPositionMeters, {10.0F, -10.0F, 0.0F});
}

bool ControlSurfaceQuaternionSignEquivalence()
{
    const auto water = F1Water(1025.0F);
    const PhysicsQuaternion q{0.2F, -0.3F, 0.4F, 0.5F};
    const PhysicsQuaternion negativeQ{-q.x, -q.y, -q.z, -q.w};
    const auto first = ControlSurfaceSystem::Calculate(
        *water, H1Component({-30.0F, 2.0F, 1.0F}), H1Kinematics({4.0F, -2.0F, 1.0F}, q), 0.6F);
    const auto second = ControlSurfaceSystem::Calculate(
        *water, H1Component({-30.0F, 2.0F, 1.0F}), H1Kinematics({4.0F, -2.0F, 1.0F}, negativeQ), 0.6F);
    return first && second && E2Near(
               first->bodyForwardSpeedMetersPerSecond, second->bodyForwardSpeedMetersPerSecond) &&
           E2VectorNear(first->worldPositionMeters, second->worldPositionMeters) &&
           E2VectorNear(first->forceNewtons, second->forceNewtons);
}

bool ControlSurfaceNormalizesNonUnitQuaternion()
{
    constexpr float HalfSqrtTwo = 0.7071067811865475F;
    const auto water = F1Water(1000.0F);
    const PhysicsQuaternion unit{0.0F, 0.0F, HalfSqrtTwo, HalfSqrtTwo};
    const PhysicsQuaternion scaled{0.0F, 0.0F, 3.0F * HalfSqrtTwo, 3.0F * HalfSqrtTwo};
    const auto first = ControlSurfaceSystem::Calculate(
        *water, H1Component(), H1Kinematics({0.0F, 2.0F, 0.0F}, unit), 0.5F);
    const auto second = ControlSurfaceSystem::Calculate(
        *water, H1Component(), H1Kinematics({0.0F, 2.0F, 0.0F}, scaled), 0.5F);
    return first && second && E2Near(
               first->bodyForwardSpeedMetersPerSecond, second->bodyForwardSpeedMetersPerSecond) &&
           E2VectorNear(first->worldPositionMeters, second->worldPositionMeters) &&
           E2VectorNear(first->forceNewtons, second->forceNewtons);
}

bool ControlSurfaceZeroAreaIsValid()
{
    const auto water = F1Water(1000.0F);
    const auto result = ControlSurfaceSystem::Calculate(
        *water, H1Component({-30.0F, 0.0F, 0.0F}, 0.0F), H1Kinematics({5.0F, 0.0F, 0.0F}), 1.0F);
    return result && result->bodyForwardSpeedMetersPerSecond == 5.0F &&
           result->forceNewtons == PhysicsVector3{};
}

bool ControlSurfaceRejectsInvalidConfiguration()
{
    const auto water = F1Water(1000.0F);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    for (const ControlSurfaceComponent& invalid : {
             H1Component({nan, 0.0F, 0.0F}),
             H1Component({0.0F, infinity, 0.0F}),
             H1Component({}, -1.0F),
             H1Component({}, nan),
             H1Component({}, infinity)})
    {
        if (!H1HasError(
                ControlSurfaceSystem::Calculate(*water, invalid, H1Kinematics(), 0.0F),
                ControlSurfaceErrorCode::InvalidConfiguration))
        {
            return false;
        }
    }
    return true;
}

bool ControlSurfaceRejectsInvalidDeflection()
{
    const auto water = F1Water(1000.0F);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    for (const float invalid : {-1.01F, 1.01F, nan, infinity})
    {
        if (!H1HasError(
                ControlSurfaceSystem::Calculate(*water, H1Component(), H1Kinematics(), invalid),
                ControlSurfaceErrorCode::InvalidDeflection))
        {
            return false;
        }
    }
    return true;
}

bool ControlSurfaceRejectsInvalidKinematics()
{
    const auto water = F1Water(1000.0F);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    const ControlSurfaceKinematics invalid[] = {
        H1Kinematics({}, {}, {nan, 0.0F, 0.0F}),
        H1Kinematics({}, {}, {0.0F, infinity, 0.0F}),
        H1Kinematics({nan, 0.0F, 0.0F}),
        H1Kinematics({0.0F, -infinity, 0.0F}),
        H1Kinematics({}, {0.0F, 0.0F, 0.0F, 0.0F}),
        H1Kinematics({}, {nan, 0.0F, 0.0F, 1.0F}),
        H1Kinematics({}, {0.0F, infinity, 0.0F, 1.0F})};
    for (const ControlSurfaceKinematics& kinematics : invalid)
    {
        if (!H1HasError(
                ControlSurfaceSystem::Calculate(*water, H1Component(), kinematics, 0.5F),
                ControlSurfaceErrorCode::InvalidKinematics))
        {
            return false;
        }
    }
    return true;
}

bool ControlSurfaceRejectsDerivedOverflow()
{
    const float maximum = (std::numeric_limits<float>::max)();
    const auto extremeWater = F1Water(maximum);
    const auto forceOverflow = ControlSurfaceSystem::Calculate(
        *extremeWater,
        H1Component({}, maximum),
        H1Kinematics({2.0F, 0.0F, 0.0F}, {}, {}),
        1.0F);
    const auto pointOverflow = ControlSurfaceSystem::Calculate(
        *F1Water(1000.0F),
        H1Component({maximum, 0.0F, 0.0F}, 0.0F),
        H1Kinematics({}, {}, {maximum, 0.0F, 0.0F}),
        0.0F);
    return H1HasError(forceOverflow, ControlSurfaceErrorCode::NonFiniteResult) &&
           H1HasError(pointOverflow, ControlSurfaceErrorCode::NonFiniteResult);
}

// ---------------------------------------------------------------------------
// M2 Slice F2: fixed-step integration of F1 outputs through generic PhysicsWorld force/torque APIs.
// ---------------------------------------------------------------------------

namespace
{
bool F2ApplyMarineFromOneSnapshot(
    DeepRun::Physics::PhysicsWorld& world,
    const DeepRun::Physics::PhysicsBodyHandle handle,
    const WaterBody& water,
    const BuoyancyComponent& buoyancy,
    const HydroDragComponent& drag)
{
    // This one GetBodyState is the authoritative beginning-of-tick snapshot for BOTH calculations.
    const auto state = world.GetBodyState(handle);
    const auto gravity = world.Gravity();
    if (!state || !gravity || !gravity->IsFinite())
    {
        return false;
    }
    const double gravityMagnitude = std::sqrt(
        static_cast<double>(gravity->x) * gravity->x + static_cast<double>(gravity->y) * gravity->y +
        static_cast<double>(gravity->z) * gravity->z);
    if (!std::isfinite(gravityMagnitude) || gravityMagnitude <= 0.0 ||
        gravityMagnitude > (std::numeric_limits<float>::max)())
    {
        return false;
    }

    const auto buoyancyResult = BuoyancySystem::Calculate(
        water,
        buoyancy,
        {.worldPositionMeters = state->position, .worldOrientation = state->orientation},
        static_cast<float>(gravityMagnitude));
    const auto dragResult = HydroDragSystem::Calculate(
        water,
        drag,
        {.worldOrientation = state->orientation,
         .worldLinearVelocityMetersPerSecond = state->linearVelocity,
         .worldAngularVelocityRadiansPerSecond = state->angularVelocity});
    if (!buoyancyResult || !dragResult)
    {
        return false;
    }

    for (const auto& point : buoyancyResult->points)
    {
        if (!world.AddForceAtWorldPosition(handle, point.forceNewtons, point.worldPositionMeters))
        {
            return false;
        }
    }
    // Current test body origin equals its center of mass, matching the F2 playground contract.
    return world.AddForceAtWorldPosition(handle, dragResult->forceNewtons, state->position) &&
           world.AddTorque(handle, dragResult->torqueNewtonMeters);
}

bool F2ApplyDragFromOneSnapshot(
    DeepRun::Physics::PhysicsWorld& world,
    const DeepRun::Physics::PhysicsBodyHandle handle,
    const WaterBody& water,
    const HydroDragComponent& drag)
{
    const auto state = world.GetBodyState(handle);
    if (!state)
    {
        return false;
    }
    const auto result = HydroDragSystem::Calculate(
        water,
        drag,
        {.worldOrientation = state->orientation,
         .worldLinearVelocityMetersPerSecond = state->linearVelocity,
         .worldAngularVelocityRadiansPerSecond = state->angularVelocity});
    return result && world.AddForceAtWorldPosition(handle, result->forceNewtons, state->position) &&
           world.AddTorque(handle, result->torqueNewtonMeters);
}
} // namespace

bool HydroDragIntegrationDampsVerticalDescentWithoutSpring()
{
    constexpr float Mass = 2000.0F;
    constexpr float Density = 1000.0F;
    constexpr float InitialVelocityY = -3.0F;
    constexpr int Steps = 120;
    DeepRun::Diagnostics::Logger logger;
    DeepRun::Physics::PhysicsWorld world(logger);
    const auto water = WaterBody::Create({.surfaceLevelY = 0.0F, .densityKgPerCubicMeter = Density});
    if (!world.Initialize() || !water)
    {
        return false;
    }
    const auto handle = world.CreateDynamicBoxBody(E3BodyInfo(Mass, {0.0F, InitialVelocityY, 0.0F}));
    const auto initial = world.GetBodyState(handle);
    const BuoyancyComponent buoyancy = E3NeutralBuoyancy(Mass, Density);
    const HydroDragComponent drag = F1Component({0.05F, 0.5F, 0.7F});
    if (!initial)
    {
        return false;
    }

    for (int step = 0; step < Steps; ++step)
    {
        if (!F2ApplyMarineFromOneSnapshot(world, handle, *water, buoyancy, drag))
        {
            return false;
        }
        world.Step(E3FixedDeltaSeconds);
        const auto current = world.GetBodyState(handle);
        if (!current || current->linearVelocity.y >= 0.0F)
        {
            return false; // drag damps descent; it must never become a spring or reverse direction
        }
    }

    const auto after = world.GetBodyState(handle);
    if (!after)
    {
        return false;
    }
    std::cout << "[F2 evidence] vertical drag: Y " << initial->position.y << " -> " << after->position.y
              << ", Vy " << initial->linearVelocity.y << " -> " << after->linearVelocity.y << '\n';
    return StateIsFinite(*after) && after->position.y < initial->position.y && after->linearVelocity.y < 0.0F &&
           std::abs(after->linearVelocity.y) < 0.8F * std::abs(InitialVelocityY) &&
           std::abs(after->position.x - initial->position.x) < 1.0e-3F &&
           std::abs(after->position.z - initial->position.z) < 1.0e-3F;
}

bool HydroDragIntegrationPreservesLinearAnisotropy()
{
    constexpr float Mass = 2000.0F;
    constexpr float Density = 1000.0F;
    constexpr int Steps = 120;
    DeepRun::Diagnostics::Logger logger;
    DeepRun::Physics::PhysicsWorld world(logger);
    const auto water = WaterBody::Create({.surfaceLevelY = 0.0F, .densityKgPerCubicMeter = Density});
    if (!world.Initialize() || !water)
    {
        return false;
    }

    auto xInfo = E3BodyInfo(Mass, {3.0F, 0.0F, 0.0F});
    auto yInfo = E3BodyInfo(Mass, {0.0F, 3.0F, 0.0F});
    xInfo.position.x = -100.0F;
    yInfo.position.x = 100.0F;
    const auto xBody = world.CreateDynamicBoxBody(xInfo);
    const auto yBody = world.CreateDynamicBoxBody(yInfo);
    const BuoyancyComponent buoyancy = E3NeutralBuoyancy(Mass, Density);
    const HydroDragComponent drag = F1Component({0.05F, 0.5F, 0.7F});
    for (int step = 0; step < Steps; ++step)
    {
        if (!F2ApplyMarineFromOneSnapshot(world, xBody, *water, buoyancy, drag) ||
            !F2ApplyMarineFromOneSnapshot(world, yBody, *water, buoyancy, drag))
        {
            return false;
        }
        world.Step(E3FixedDeltaSeconds);
    }

    const auto xAfter = world.GetBodyState(xBody);
    const auto yAfter = world.GetBodyState(yBody);
    if (!xAfter || !yAfter)
    {
        return false;
    }
    std::cout << "[F2 evidence] anisotropy: Vx(Ax=0.05) " << xAfter->linearVelocity.x
              << ", Vy(Ay=0.5) " << yAfter->linearVelocity.y << '\n';
    return StateIsFinite(*xAfter) && StateIsFinite(*yAfter) && xAfter->linearVelocity.x > 0.0F &&
           yAfter->linearVelocity.y > 0.0F && xAfter->linearVelocity.x > yAfter->linearVelocity.y + 0.5F;
}

bool HydroDragIntegrationUsesBodyLocalAxes()
{
    constexpr float HalfSqrtTwo = 0.7071067811865475F;
    DeepRun::Diagnostics::Logger logger;
    DeepRun::Physics::PhysicsWorld world(logger);
    const auto water = F1Water(1000.0F);
    if (!world.Initialize() || !water)
    {
        return false;
    }
    auto info = E3BodyInfo(2000.0F, {0.0F, 3.0F, 0.0F}, {0.0F, 0.0F, HalfSqrtTwo, HalfSqrtTwo});
    info.gravityEnabled = false;
    const auto handle = world.CreateDynamicBoxBody(info);
    const auto initial = world.GetBodyState(handle);
    const HydroDragComponent drag = F1Component({0.5F, 0.0F, 0.0F});
    for (int step = 0; step < 120; ++step)
    {
        if (!F2ApplyDragFromOneSnapshot(world, handle, *water, drag))
        {
            return false;
        }
        world.Step(E3FixedDeltaSeconds);
    }
    const auto after = world.GetBodyState(handle);
    std::cout << "[F2 evidence] rotated local X along world Y: Vy "
              << (initial ? initial->linearVelocity.y : 0.0F) << " -> "
              << (after ? after->linearVelocity.y : 0.0F) << '\n';
    return initial && after && StateIsFinite(*after) && after->linearVelocity.y > 0.0F &&
           after->linearVelocity.y < 0.8F * initial->linearVelocity.y &&
           std::abs(after->linearVelocity.x) < 1.0e-3F && std::abs(after->linearVelocity.z) < 1.0e-3F;
}

bool HydroDragIntegrationDampsAngularVelocity()
{
    DeepRun::Diagnostics::Logger logger;
    DeepRun::Physics::PhysicsWorld world(logger);
    const auto water = F1Water(1000.0F);
    if (!world.Initialize() || !water)
    {
        return false;
    }
    auto info = E3BodyInfo(2000.0F);
    info.gravityEnabled = false;
    info.initialAngularVelocity = {0.0F, 0.0F, 1.0F};
    const auto handle = world.CreateDynamicBoxBody(info);
    const auto initial = world.GetBodyState(handle);
    const HydroDragComponent drag = F1Component({}, {0.0F, 0.0F, 5.0F});
    for (int step = 0; step < 60; ++step)
    {
        if (!F2ApplyDragFromOneSnapshot(world, handle, *water, drag))
        {
            return false;
        }
        world.Step(E3FixedDeltaSeconds);
    }
    const auto after = world.GetBodyState(handle);
    std::cout << "[F2 evidence] angular drag: omegaZ "
              << (initial ? initial->angularVelocity.z : 0.0F) << " -> "
              << (after ? after->angularVelocity.z : 0.0F) << '\n';
    return initial && after && StateIsFinite(*after) && after->angularVelocity.z > 0.0F &&
           after->angularVelocity.z < 0.9F * initial->angularVelocity.z &&
           after->linearVelocity == PhysicsVector3{};
}

// ---------------------------------------------------------------------------
// M2 Slice G1: pure deterministic one-shaft RPM evolution and scalar thrust output.
// ---------------------------------------------------------------------------

namespace
{
using DeepRun::Marine::PropulsionCommand;
using DeepRun::Marine::PropulsionComponent;
using DeepRun::Marine::PropulsionError;
using DeepRun::Marine::PropulsionErrorCode;
using DeepRun::Marine::PropulsionResult;
using DeepRun::Marine::PropulsionState;
using DeepRun::Marine::PropulsionSystem;

PropulsionComponent G1Component()
{
    return PropulsionComponent{
        .maxForwardRpm = 120.0F,
        .maxReverseRpm = 80.0F,
        .maxForwardThrustNewtons = 12'000.0F,
        .maxReverseThrustNewtons = 6'000.0F,
        .spinUpRateRpmPerSecond = 60.0F,
        .spinDownRateRpmPerSecond = 30.0F};
}

bool G1HasError(
    const std::expected<PropulsionResult, PropulsionError>& result,
    const PropulsionErrorCode code)
{
    return !result && result.error().code == code && !result.error().message.empty();
}
} // namespace

bool PropulsionIdleIsExactZero()
{
    const auto result = PropulsionSystem::Advance(
        G1Component(), {}, {.requestedDriveFraction = 0.0F, .availablePowerFraction = 1.0F}, 1.0F / 60.0F);
    return result && result->nextState.shaftRpm == 0.0F && result->effectiveDriveFraction == 0.0F &&
           result->targetRpm == 0.0F && result->thrustNewtons == 0.0F;
}

bool PropulsionZeroPowerAtRestIsExactZero()
{
    const auto result = PropulsionSystem::Advance(
        G1Component(), {}, {.requestedDriveFraction = -1.0F, .availablePowerFraction = 0.0F}, 0.25F);
    return result && result->nextState.shaftRpm == 0.0F && result->effectiveDriveFraction == 0.0F &&
           result->targetRpm == 0.0F && result->thrustNewtons == 0.0F;
}

bool PropulsionHasFiniteSpinUp()
{
    const PropulsionComponent component = G1Component();
    const auto result = PropulsionSystem::Advance(
        component, {}, {.requestedDriveFraction = 1.0F, .availablePowerFraction = 1.0F}, 1.0F / 60.0F);
    return result && result->targetRpm == component.maxForwardRpm && result->nextState.shaftRpm > 0.0F &&
           result->nextState.shaftRpm < component.maxForwardRpm && result->thrustNewtons > 0.0F &&
           result->thrustNewtons < component.maxForwardThrustNewtons;
}

bool PropulsionUsesConfiguredRpmRate()
{
    const auto result = PropulsionSystem::Advance(
        G1Component(), {}, {.requestedDriveFraction = 1.0F, .availablePowerFraction = 1.0F}, 0.5F);
    return result && std::abs(result->nextState.shaftRpm - 30.0F) < 1.0e-5F &&
           std::abs(result->thrustNewtons - 750.0F) < 1.0e-3F;
}

bool PropulsionReachesTargetWithoutOvershoot()
{
    const PropulsionComponent component = G1Component();
    const PropulsionCommand command{.requestedDriveFraction = 1.0F, .availablePowerFraction = 1.0F};
    PropulsionState state{};
    for (int step = 0; step < 4; ++step)
    {
        const auto result = PropulsionSystem::Advance(component, state, command, 0.5F);
        if (!result || result->nextState.shaftRpm > component.maxForwardRpm)
        {
            return false;
        }
        state = result->nextState;
    }
    const auto stable = PropulsionSystem::Advance(component, state, command, 0.5F);
    return state.shaftRpm == component.maxForwardRpm && stable &&
           stable->nextState.shaftRpm == component.maxForwardRpm &&
           stable->thrustNewtons == component.maxForwardThrustNewtons;
}

bool PropulsionIsStepSizeConsistent()
{
    const PropulsionComponent component = G1Component();
    const PropulsionCommand command{.requestedDriveFraction = 1.0F, .availablePowerFraction = 1.0F};
    const auto simulate = [&component, &command](const int steps, const float delta) -> std::optional<float> {
        PropulsionState state{};
        for (int step = 0; step < steps; ++step)
        {
            const auto result = PropulsionSystem::Advance(component, state, command, delta);
            if (!result)
            {
                return std::nullopt;
            }
            state = result->nextState;
        }
        return state.shaftRpm;
    };
    const auto sixtyHz = simulate(60, 1.0F / 60.0F);
    const auto oneTwentyHz = simulate(120, 1.0F / 120.0F);
    return sixtyHz && oneTwentyHz && std::abs(*sixtyHz - 60.0F) < 1.0e-3F &&
           std::abs(*sixtyHz - *oneTwentyHz) < 1.0e-3F;
}

bool PropulsionDriveReductionSpinsDown()
{
    const PropulsionComponent component = G1Component();
    const PropulsionCommand neutral{.requestedDriveFraction = 0.0F, .availablePowerFraction = 1.0F};
    const auto first = PropulsionSystem::Advance(component, {.shaftRpm = 90.0F}, neutral, 1.0F);
    if (!first || first->nextState.shaftRpm != 60.0F || first->targetRpm != 0.0F ||
        first->thrustNewtons <= 0.0F || first->thrustNewtons >= 6'750.0F)
    {
        return false;
    }
    const auto second = PropulsionSystem::Advance(component, first->nextState, neutral, 1.0F);
    const auto stopped = second ? PropulsionSystem::Advance(component, second->nextState, neutral, 1.0F)
                                : std::expected<PropulsionResult, PropulsionError>{
                                      std::unexpected(PropulsionError{})};
    return second && stopped && stopped->nextState.shaftRpm == 0.0F && stopped->thrustNewtons == 0.0F;
}

bool PropulsionPowerLossCoastsDown()
{
    const PropulsionComponent component = G1Component();
    const auto result = PropulsionSystem::Advance(
        component,
        {.shaftRpm = 80.0F},
        {.requestedDriveFraction = 1.0F, .availablePowerFraction = 0.0F},
        1.0F);
    return result && result->effectiveDriveFraction == 0.0F && result->targetRpm == 0.0F &&
           result->nextState.shaftRpm == 50.0F && result->thrustNewtons > 0.0F &&
           result->thrustNewtons < component.maxForwardThrustNewtons;
}

bool PropulsionLimitedPowerSetsReducedTarget()
{
    const PropulsionComponent component = G1Component();
    const PropulsionCommand limited{.requestedDriveFraction = 1.0F, .availablePowerFraction = 0.5F};
    const auto reached = PropulsionSystem::Advance(component, {}, limited, 1.0F);
    const auto stable = reached ? PropulsionSystem::Advance(component, reached->nextState, limited, 1.0F)
                                : std::expected<PropulsionResult, PropulsionError>{
                                      std::unexpected(PropulsionError{})};
    return reached && stable && reached->effectiveDriveFraction == 0.5F && reached->targetRpm == 60.0F &&
           stable->nextState.shaftRpm == 60.0F &&
           std::abs(stable->thrustNewtons - 0.25F * component.maxForwardThrustNewtons) < 1.0e-3F;
}

bool PropulsionSupportsReverse()
{
    const PropulsionComponent component = G1Component();
    const PropulsionCommand reverse{.requestedDriveFraction = -1.0F, .availablePowerFraction = 1.0F};
    PropulsionState state{};
    for (int step = 0; step < 3; ++step)
    {
        const auto result = PropulsionSystem::Advance(component, state, reverse, 0.5F);
        if (!result || result->nextState.shaftRpm >= 0.0F || result->thrustNewtons >= 0.0F)
        {
            return false;
        }
        state = result->nextState;
    }
    const auto stable = PropulsionSystem::Advance(component, state, reverse, 0.5F);
    return stable && state.shaftRpm == -component.maxReverseRpm &&
           stable->thrustNewtons == -component.maxReverseThrustNewtons;
}

bool PropulsionDirectionChangePassesThroughZero()
{
    const PropulsionComponent component = G1Component();
    const PropulsionCommand reverse{.requestedDriveFraction = -1.0F, .availablePowerFraction = 1.0F};
    const auto towardZero = PropulsionSystem::Advance(component, {.shaftRpm = 30.0F}, reverse, 0.5F);
    const auto atZero = towardZero ? PropulsionSystem::Advance(component, towardZero->nextState, reverse, 1.0F)
                                   : std::expected<PropulsionResult, PropulsionError>{
                                         std::unexpected(PropulsionError{})};
    const auto reversing = atZero ? PropulsionSystem::Advance(component, atZero->nextState, reverse, 0.5F)
                                  : std::expected<PropulsionResult, PropulsionError>{
                                        std::unexpected(PropulsionError{})};
    return towardZero && atZero && reversing && towardZero->nextState.shaftRpm == 15.0F &&
           towardZero->thrustNewtons > 0.0F && atZero->nextState.shaftRpm == 0.0F &&
           atZero->thrustNewtons == 0.0F && reversing->nextState.shaftRpm == -30.0F &&
           reversing->thrustNewtons < 0.0F;
}

bool PropulsionThrustIsQuadraticInRpm()
{
    const PropulsionComponent component = G1Component();
    const auto forwardHalf = PropulsionSystem::Advance(
        component,
        {.shaftRpm = 60.0F},
        {.requestedDriveFraction = 0.5F, .availablePowerFraction = 1.0F},
        0.1F);
    const auto forwardFull = PropulsionSystem::Advance(
        component,
        {.shaftRpm = 120.0F},
        {.requestedDriveFraction = 1.0F, .availablePowerFraction = 1.0F},
        0.1F);
    const auto reverseHalf = PropulsionSystem::Advance(
        component,
        {.shaftRpm = -40.0F},
        {.requestedDriveFraction = -0.5F, .availablePowerFraction = 1.0F},
        0.1F);
    return forwardHalf && forwardFull && reverseHalf &&
           std::abs(4.0F * forwardHalf->thrustNewtons - forwardFull->thrustNewtons) < 1.0e-3F &&
           std::abs(reverseHalf->thrustNewtons + 0.25F * component.maxReverseThrustNewtons) < 1.0e-3F;
}

bool PropulsionUsesAsymmetricAheadAsternLimits()
{
    PropulsionComponent component = G1Component();
    component.maxForwardRpm = 150.0F;
    component.maxReverseRpm = 60.0F;
    component.maxForwardThrustNewtons = 8'000.0F;
    component.maxReverseThrustNewtons = 3'000.0F;
    const auto ahead = PropulsionSystem::Advance(
        component,
        {.shaftRpm = 150.0F},
        {.requestedDriveFraction = 1.0F, .availablePowerFraction = 1.0F},
        0.1F);
    const auto astern = PropulsionSystem::Advance(
        component,
        {.shaftRpm = -60.0F},
        {.requestedDriveFraction = -1.0F, .availablePowerFraction = 1.0F},
        0.1F);
    return ahead && astern && ahead->targetRpm == 150.0F && ahead->thrustNewtons == 8'000.0F &&
           astern->targetRpm == -60.0F && astern->thrustNewtons == -3'000.0F;
}

bool PropulsionRejectsInvalidConfiguration()
{
    using Field = float PropulsionComponent::*;
    constexpr Field Fields[] = {
        &PropulsionComponent::maxForwardRpm,
        &PropulsionComponent::maxReverseRpm,
        &PropulsionComponent::maxForwardThrustNewtons,
        &PropulsionComponent::maxReverseThrustNewtons,
        &PropulsionComponent::spinUpRateRpmPerSecond,
        &PropulsionComponent::spinDownRateRpmPerSecond};
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    for (const Field field : Fields)
    {
        for (const float invalid : {0.0F, -1.0F, nan, infinity})
        {
            PropulsionComponent component = G1Component();
            component.*field = invalid;
            if (!G1HasError(
                    PropulsionSystem::Advance(
                        component, {}, {.requestedDriveFraction = 0.0F, .availablePowerFraction = 1.0F}, 0.1F),
                    PropulsionErrorCode::InvalidConfiguration))
            {
                return false;
            }
        }
    }
    return true;
}

bool PropulsionRejectsInvalidCommand()
{
    const PropulsionComponent component = G1Component();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    for (const float invalidDrive : {-1.01F, 1.01F, nan, infinity})
    {
        if (!G1HasError(
                PropulsionSystem::Advance(
                    component,
                    {},
                    {.requestedDriveFraction = invalidDrive, .availablePowerFraction = 1.0F},
                    0.1F),
                PropulsionErrorCode::InvalidCommand))
        {
            return false;
        }
    }
    for (const float invalidPower : {-0.01F, 1.01F, nan, infinity})
    {
        if (!G1HasError(
                PropulsionSystem::Advance(
                    component,
                    {},
                    {.requestedDriveFraction = 0.0F, .availablePowerFraction = invalidPower},
                    0.1F),
                PropulsionErrorCode::InvalidCommand))
        {
            return false;
        }
    }
    return true;
}

bool PropulsionRejectsInvalidStateAndDeltaTime()
{
    const PropulsionComponent component = G1Component();
    const PropulsionCommand command{.requestedDriveFraction = 0.0F, .availablePowerFraction = 1.0F};
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    for (const float invalidRpm : {nan, infinity, -infinity, 120.01F, -80.01F})
    {
        if (!G1HasError(
                PropulsionSystem::Advance(component, {.shaftRpm = invalidRpm}, command, 0.1F),
                PropulsionErrorCode::InvalidState))
        {
            return false;
        }
    }
    for (const float invalidDelta : {0.0F, -0.1F, nan, infinity})
    {
        if (!G1HasError(
                PropulsionSystem::Advance(component, {}, command, invalidDelta),
                PropulsionErrorCode::InvalidDeltaTime))
        {
            return false;
        }
    }
    return true;
}

bool PropulsionRejectsDerivedOverflow()
{
    const float maximum = (std::numeric_limits<float>::max)();
    PropulsionComponent component = G1Component();
    component.maxForwardRpm = maximum;
    component.spinUpRateRpmPerSecond = maximum;
    const auto result = PropulsionSystem::Advance(
        component,
        {},
        {.requestedDriveFraction = 1.0F, .availablePowerFraction = 1.0F},
        maximum);
    return G1HasError(result, PropulsionErrorCode::NonFiniteResult);
}

// ---------------------------------------------------------------------------
// M2 Slice G2: public-API marine/physics integration and RPM-driven per-node presentation.
// ---------------------------------------------------------------------------

namespace
{
constexpr PropulsionComponent G2Component{
    .maxForwardRpm = 180.0F,
    .maxReverseRpm = 120.0F,
    .maxForwardThrustNewtons = 12'000'000.0F,
    .maxReverseThrustNewtons = 4'800'000.0F,
    .spinUpRateRpmPerSecond = 30.0F,
    .spinDownRateRpmPerSecond = 45.0F};
constexpr HydroDragComponent G2Drag{
    .linearEffectiveAreaSquareMeters = {150.0F, 1800.0F, 2200.0F},
    .angularEffectiveMomentMeters5 = {0.0F, 0.0F, 50'000'000.0F}};
constexpr PhysicsVector3 G2PropulsorBodyLocal{-50.0F, 0.0F, 0.0F};
constexpr float G2PropulsorAlignmentToleranceMeters = 2.0F;

struct G2TickSample final
{
    PhysicsBodyState before{};
    PhysicsBodyState after{};
    float shaftRpm = 0.0F;
    float targetRpm = 0.0F;
    float thrustNewtons = 0.0F;
    float dragXNewtons = 0.0F;
};

bool G2ApplyMarineTick(
    DeepRun::Physics::PhysicsWorld& world,
    const DeepRun::Physics::PhysicsBodyHandle handle,
    const WaterBody& water,
    const BuoyancyComponent& buoyancy,
    const PropulsionCommand& command,
    PropulsionState* propulsionState,
    G2TickSample* sample = nullptr)
{
    const auto state = world.GetBodyState(handle); // exactly one authoritative beginning-of-tick snapshot
    const auto gravity = world.Gravity();
    if (!state || !gravity || !gravity->IsFinite())
    {
        return false;
    }
    const double gravityMagnitude = std::sqrt(
        static_cast<double>(gravity->x) * gravity->x + static_cast<double>(gravity->y) * gravity->y +
        static_cast<double>(gravity->z) * gravity->z);
    const auto buoyancyResult = BuoyancySystem::Calculate(
        water,
        buoyancy,
        {.worldPositionMeters = state->position, .worldOrientation = state->orientation},
        static_cast<float>(gravityMagnitude));
    const auto dragResult = HydroDragSystem::Calculate(
        water,
        G2Drag,
        {.worldOrientation = state->orientation,
         .worldLinearVelocityMetersPerSecond = state->linearVelocity,
         .worldAngularVelocityRadiansPerSecond = state->angularVelocity});
    const auto propulsionResult = PropulsionSystem::Advance(
        G2Component, *propulsionState, command, E3FixedDeltaSeconds);
    if (!buoyancyResult || !dragResult || !propulsionResult)
    {
        return false;
    }
    const auto force = DeepRun::Game::RotateBodyLocalVectorToWorld(
        state->orientation, {propulsionResult->thrustNewtons, 0.0F, 0.0F});
    const auto point = DeepRun::Game::TransformBodyLocalPointToWorld(
        state->position, state->orientation, G2PropulsorBodyLocal);
    if (!force || !point)
    {
        return false;
    }

    for (const auto& buoyancyPoint : buoyancyResult->points)
    {
        if (!world.AddForceAtWorldPosition(handle, buoyancyPoint.forceNewtons, buoyancyPoint.worldPositionMeters))
        {
            return false;
        }
    }
    if (!world.AddForceAtWorldPosition(handle, dragResult->forceNewtons, state->position) ||
        !world.AddTorque(handle, dragResult->torqueNewtonMeters) ||
        !world.AddForceAtWorldPosition(handle, *force, *point))
    {
        return false;
    }

    *propulsionState = propulsionResult->nextState; // transactional commit after every application succeeds
    world.Step(E3FixedDeltaSeconds);
    const auto after = world.GetBodyState(handle);
    if (!after)
    {
        return false;
    }
    if (sample != nullptr)
    {
        *sample = {
            .before = *state,
            .after = *after,
            .shaftRpm = propulsionResult->nextState.shaftRpm,
            .targetRpm = propulsionResult->targetRpm,
            .thrustNewtons = propulsionResult->thrustNewtons,
            .dragXNewtons = dragResult->forceNewtons.x};
    }
    return true;
}

DeepRun::Assets::ModelAsset G2SyntheticThreeNodeModel()
{
    const auto id = DeepRun::Assets::AssetId::FromPath("Tests/G2Synthetic.gltf");
    std::vector<DeepRun::Assets::MeshPrimitiveData> primitives(3);
    primitives[0].indices.resize(3);
    primitives[1].indices.resize(6);
    primitives[2].indices.resize(9);
    std::vector<DeepRun::Assets::MeshNodeData> nodes{
        {.name = "Hull", .localToModel = {}, .primitiveIndices = {0}},
        {.name = "PresentationNode",
         .localToModel = DeepRun::Game::TranslationTransform({-49.0F, 0.0F, 0.0F}),
         .primitiveIndices = {1}},
        {.name = "Sail", .localToModel = {}, .primitiveIndices = {2}}};
    return {.id = *id, .primitives = std::move(primitives), .nodes = std::move(nodes)};
}
} // namespace

bool G2BodyLocalThrustAndPointTransform()
{
    constexpr float HalfSqrtTwo = 0.7071067811865475F;
    const PhysicsQuaternion quarterTurn{0.0F, 0.0F, HalfSqrtTwo, HalfSqrtTwo};
    const auto force = DeepRun::Game::RotateBodyLocalVectorToWorld(quarterTurn, {10.0F, 0.0F, 0.0F});
    const auto point = DeepRun::Game::TransformBodyLocalPointToWorld(
        {10.0F, 20.0F, 0.0F}, quarterTurn, G2PropulsorBodyLocal);
    const PhysicsQuaternion zero{};
    const float nan = std::numeric_limits<float>::quiet_NaN();
    return force && point && std::abs(force->x) < 1.0e-4F && std::abs(force->y - 10.0F) < 1.0e-4F &&
           std::abs(point->x - 10.0F) < 1.0e-4F && std::abs(point->y + 30.0F) < 1.0e-4F &&
           !DeepRun::Game::RotateBodyLocalVectorToWorld({0.0F, 0.0F, 0.0F, 0.0F}, {}) &&
           !DeepRun::Game::TransformBodyLocalPointToWorld({nan, 0.0F, 0.0F}, zero, {});
}

bool G2PropellerPresentationFollowsRpm()
{
    constexpr float Pi = 3.14159265358979323846F;
    const auto positive = DeepRun::Game::AdvancePropellerPresentationAngle(1.0F, 0.0F, 30.0F, 0.5F);
    const auto negative = DeepRun::Game::AdvancePropellerPresentationAngle(1.0F, 0.0F, -30.0F, 0.5F);
    const auto zero = DeepRun::Game::AdvancePropellerPresentationAngle(1.0F, 0.0F, 0.0F, 0.5F);
    const auto wrapped = DeepRun::Game::AdvancePropellerPresentationAngle(6.0F, 60.0F, 60.0F, 1.0F);
    const auto identity = DeepRun::Game::RotationXTransform(0.0F);
    const auto quarterTurn = DeepRun::Game::RotationXTransform(0.5F * Pi);
    if (!positive || !negative || !zero || !wrapped || !identity || !quarterTurn)
    {
        return false;
    }
    const ModelVector3 y = TransformPointBy(*quarterTurn, {0.0F, 1.0F, 0.0F});
    const ModelVector3 z = TransformPointBy(*quarterTurn, {0.0F, 0.0F, 1.0F});
    return *positive > 1.0F && *negative < 1.0F && *zero == 1.0F && *wrapped >= 0.0F &&
           *wrapped < 2.0F * Pi && identity->values == ModelTransform{}.values &&
           std::abs(y.y) < 1.0e-4F && std::abs(y.z - 1.0F) < 1.0e-4F &&
           std::abs(z.y + 1.0F) < 1.0e-4F && std::abs(z.z) < 1.0e-4F &&
           !DeepRun::Game::RotationXTransform(std::numeric_limits<float>::infinity());
}

bool G2PropellerNodeContractValidation()
{
    DeepRun::Assets::AssetManager assets(testAssetRoot);
    const auto loaded = assets.LoadModel(CanonicalModelPath);
    if (!loaded)
    {
        return false;
    }
    const ModelVector3 center = DeepRun::Game::BoundsCenter((*loaded)->bounds);
    const auto propeller = std::ranges::find_if((*loaded)->nodes, [](const DeepRun::Assets::MeshNodeData& node) {
        return node.name == DeepRun::Game::M2PrototypePropellerNodeName;
    });
    if (propeller == (*loaded)->nodes.end())
    {
        return false;
    }
    std::cout << "[G2 evidence] asset center (" << center.x << ',' << center.y << ',' << center.z
              << "), authored hub (" << propeller->localToModel.values[12] << ','
              << propeller->localToModel.values[13] << ',' << propeller->localToModel.values[14] << ")\n";
    const auto canonical = DeepRun::Game::ResolveM2PrototypePropellerNode(
        **loaded, center, G2PropulsorBodyLocal, G2PropulsorAlignmentToleranceMeters);
    if (!canonical || (*loaded)->nodes[*canonical].name != DeepRun::Game::M2PrototypePropellerNodeName)
    {
        return false;
    }

    DeepRun::Assets::ModelAsset missing = **loaded;
    missing.nodes[*canonical].name = "Missing";
    DeepRun::Assets::ModelAsset duplicate = **loaded;
    duplicate.nodes.push_back(duplicate.nodes[*canonical]);
    DeepRun::Assets::ModelAsset nonFinite = **loaded;
    nonFinite.nodes[*canonical].localToModel.values[12] = std::numeric_limits<float>::quiet_NaN();
    std::cout << "[G2 evidence] canonical propeller node index " << *canonical << ", body-local hub (-50,0,0)\n";
    return !DeepRun::Game::ResolveM2PrototypePropellerNode(
               missing, center, G2PropulsorBodyLocal, G2PropulsorAlignmentToleranceMeters) &&
           !DeepRun::Game::ResolveM2PrototypePropellerNode(
               duplicate, center, G2PropulsorBodyLocal, G2PropulsorAlignmentToleranceMeters) &&
           !DeepRun::Game::ResolveM2PrototypePropellerNode(
               nonFinite, center, G2PropulsorBodyLocal, G2PropulsorAlignmentToleranceMeters) &&
           !DeepRun::Game::ResolveM2PrototypePropellerNode(
               **loaded, center, {-40.0F, 0.0F, 0.0F}, G2PropulsorAlignmentToleranceMeters);
}

bool G2NodeOverrideValidation()
{
    const DeepRun::Assets::ModelAsset model = G2SyntheticThreeNodeModel();
    const auto empty = DeepRun::Render::PrepareModelDraws(model);
    const auto baseline = DeepRun::Render::PrepareModelDraws(model, {}, {});
    DeepRun::Render::ModelNodeTransformOverride outOfRange{.nodeIndex = 3};
    const DeepRun::Render::ModelNodeTransformOverride duplicate[] = {{.nodeIndex = 1}, {.nodeIndex = 1}};
    DeepRun::Render::ModelNodeTransformOverride nonFinite{.nodeIndex = 1};
    nonFinite.nodeLocalPostTransform.values[0] = std::numeric_limits<float>::quiet_NaN();
    DeepRun::Render::ModelNodeTransformOverride nonAffine{.nodeIndex = 1};
    nonAffine.nodeLocalPostTransform.values[15] = 2.0F;
    return empty && baseline && empty->size() == baseline->size() &&
           (*empty)[1].modelToWorld.values == (*baseline)[1].modelToWorld.values &&
           !DeepRun::Render::PrepareModelDraws(model, {}, std::span{&outOfRange, 1}) &&
           !DeepRun::Render::PrepareModelDraws(model, {}, duplicate) &&
           !DeepRun::Render::PrepareModelDraws(model, {}, std::span{&nonFinite, 1}) &&
           !DeepRun::Render::PrepareModelDraws(model, {}, std::span{&nonAffine, 1});
}

bool G2NodePostTransformKeepsHubAndOtherNodes()
{
    constexpr float Pi = 3.14159265358979323846F;
    const DeepRun::Assets::ModelAsset model = G2SyntheticThreeNodeModel();
    const auto rotation = DeepRun::Game::RotationXTransform(0.5F * Pi);
    if (!rotation)
    {
        return false;
    }
    const DeepRun::Render::ModelNodeTransformOverride overrideValue{
        .nodeIndex = 1, .nodeLocalPostTransform = *rotation};
    const auto baseline = DeepRun::Render::PrepareModelDraws(model);
    const auto animated = DeepRun::Render::PrepareModelDraws(model, {}, std::span{&overrideValue, 1});
    if (!baseline || !animated || baseline->size() != 3 || animated->size() != 3)
    {
        return false;
    }
    const ModelTransform& hub = (*animated)[1].modelToWorld;
    const std::size_t submittedIndices = model.primitives[0].indices.size() +
                                         model.primitives[1].indices.size() +
                                         model.primitives[2].indices.size();
    return hub.values[12] == -49.0F && hub.values[13] == 0.0F && hub.values[14] == 0.0F &&
           (*animated)[0].modelToWorld.values == (*baseline)[0].modelToWorld.values &&
           (*animated)[2].modelToWorld.values == (*baseline)[2].modelToWorld.values &&
           (*animated)[1].modelToWorld.values != (*baseline)[1].modelToWorld.values &&
           submittedIndices == 18;
}

bool G2CanonicalDrawAndBoundsGate()
{
    constexpr float Pi = 3.14159265358979323846F;
    DeepRun::Assets::AssetManager assets(testAssetRoot);
    const auto loaded = assets.LoadModel(CanonicalModelPath);
    if (!loaded)
    {
        return false;
    }
    const ModelVector3 center = DeepRun::Game::BoundsCenter((*loaded)->bounds);
    const auto nodeIndex = DeepRun::Game::ResolveM2PrototypePropellerNode(
        **loaded, center, G2PropulsorBodyLocal, G2PropulsorAlignmentToleranceMeters);
    const auto rotation = DeepRun::Game::RotationXTransform(0.5F * Pi);
    if (!nodeIndex || !rotation)
    {
        return false;
    }
    const DeepRun::Render::ModelNodeTransformOverride overrideValue{
        .nodeIndex = *nodeIndex, .nodeLocalPostTransform = *rotation};
    const auto baseline = DeepRun::Render::PrepareModelDraws(**loaded);
    const auto animated = DeepRun::Render::PrepareModelDraws(**loaded, {}, std::span{&overrideValue, 1});
    if (!baseline || !animated || baseline->size() != 4 || animated->size() != 4)
    {
        return false;
    }
    std::size_t changed = 0;
    std::uint64_t submittedIndices = 0;
    for (std::size_t index = 0; index < animated->size(); ++index)
    {
        submittedIndices += (*loaded)->primitives[(*animated)[index].primitiveIndex].indices.size();
        changed += (*animated)[index].modelToWorld.values != (*baseline)[index].modelToWorld.values ? 1U : 0U;
    }

    const auto& node = (*loaded)->nodes[*nodeIndex];
    const auto& primitive = (*loaded)->primitives[node.primitiveIndices.front()];
    for (const float angle : {0.0F, 0.5F * Pi, Pi, 1.5F * Pi})
    {
        const auto spin = DeepRun::Game::RotationXTransform(angle);
        const ModelTransform combined = DeepRun::Render::Multiply(node.localToModel, *spin);
        for (const auto& vertex : primitive.vertices)
        {
            const ModelVector3 point = TransformPointBy(combined, vertex.position);
            if (point.x < (*loaded)->bounds.minimum.x - 1.0e-3F ||
                point.x > (*loaded)->bounds.maximum.x + 1.0e-3F ||
                point.y < (*loaded)->bounds.minimum.y - 1.0e-3F ||
                point.y > (*loaded)->bounds.maximum.y + 1.0e-3F ||
                point.z < (*loaded)->bounds.minimum.z - 1.0e-3F ||
                point.z > (*loaded)->bounds.maximum.z + 1.0e-3F)
            {
                return false;
            }
        }
    }
    return changed == 1 && submittedIndices == 1632;
}

bool G2AheadAccelerationAndRpmRamp()
{
    constexpr float Mass = 1'200'000.0F;
    constexpr float Density = 1025.0F;
    DeepRun::Diagnostics::Logger logger;
    DeepRun::Physics::PhysicsWorld world(logger);
    const auto water = WaterBody::Create({.surfaceLevelY = 0.0F, .densityKgPerCubicMeter = Density});
    if (!world.Initialize() || !water)
    {
        return false;
    }
    const auto handle = world.CreateDynamicBoxBody(E3BodyInfo(Mass));
    const auto initial = world.GetBodyState(handle);
    const BuoyancyComponent buoyancy = E3NeutralBuoyancy(Mass, Density);
    const PropulsionCommand ahead{.requestedDriveFraction = 1.0F, .availablePowerFraction = 1.0F};
    PropulsionState propulsion{};
    G2TickSample oneSecond;
    G2TickSample twoSeconds;
    for (int step = 0; step < 120; ++step)
    {
        G2TickSample sample;
        if (!G2ApplyMarineTick(world, handle, *water, buoyancy, ahead, &propulsion, &sample))
        {
            return false;
        }
        if (step == 59) oneSecond = sample;
        if (step == 119) twoSeconds = sample;
    }
    const auto after = world.GetBodyState(handle);
    return initial && after && StateIsFinite(*after) && std::abs(oneSecond.shaftRpm - 30.0F) < 0.01F &&
           std::abs(twoSeconds.shaftRpm - 60.0F) < 0.01F && twoSeconds.thrustNewtons > oneSecond.thrustNewtons &&
           std::abs(twoSeconds.dragXNewtons) > std::abs(oneSecond.dragXNewtons) &&
           after->linearVelocity.x > 0.0F && after->position.x > initial->position.x;
}

bool G2NaturalTerminalSpeedAndHydrostaticStability()
{
    constexpr float Mass = 1'200'000.0F;
    constexpr float Density = 1025.0F;
    constexpr int Steps = 1800; // 30 seconds: six-second spin-up plus ample settling time
    DeepRun::Diagnostics::Logger logger;
    DeepRun::Physics::PhysicsWorld world(logger);
    const auto water = WaterBody::Create({.surfaceLevelY = 0.0F, .densityKgPerCubicMeter = Density});
    if (!world.Initialize() || !water)
    {
        return false;
    }
    const auto handle = world.CreateDynamicBoxBody(E3BodyInfo(Mass));
    const auto initial = world.GetBodyState(handle);
    const BuoyancyComponent buoyancy = E3NeutralBuoyancy(Mass, Density);
    const PropulsionCommand ahead{.requestedDriveFraction = 1.0F, .availablePowerFraction = 1.0F};
    PropulsionState propulsion{};
    G2TickSample finalSample;
    float speedOneSecondBeforeEnd = 0.0F;
    for (int step = 0; step < Steps; ++step)
    {
        if (!G2ApplyMarineTick(world, handle, *water, buoyancy, ahead, &propulsion, &finalSample))
        {
            return false;
        }
        if (step == Steps - 61)
        {
            speedOneSecondBeforeEnd = finalSample.after.linearVelocity.x;
        }
    }
    const auto after = world.GetBodyState(handle);
    if (!initial || !after)
    {
        return false;
    }
    const float analytic = std::sqrt(
        G2Component.maxForwardThrustNewtons / (0.5F * Density * G2Drag.linearEffectiveAreaSquareMeters.x));
    std::cout << "[G2 evidence] terminal: RPM " << finalSample.shaftRpm << ", thrust "
              << finalSample.thrustNewtons << " N, Vx " << after->linearVelocity.x << " m/s, dragX "
              << finalSample.dragXNewtons << " N, analytic " << analytic << " m/s, Y "
              << after->position.y << ", Vy " << after->linearVelocity.y << ", omegaZ "
              << after->angularVelocity.z << '\n';
    return StateIsFinite(*after) && finalSample.shaftRpm == G2Component.maxForwardRpm &&
           std::abs(finalSample.thrustNewtons - G2Component.maxForwardThrustNewtons) < 1.0F &&
           std::abs(std::abs(finalSample.dragXNewtons) - finalSample.thrustNewtons) <
               0.03F * finalSample.thrustNewtons &&
           std::abs(after->linearVelocity.x - analytic) < 0.05F * analytic &&
           std::abs(after->linearVelocity.x - speedOneSecondBeforeEnd) < 0.05F &&
           std::abs(after->position.y - initial->position.y) < 0.25F &&
           std::abs(after->linearVelocity.y) < 0.05F && std::abs(after->angularVelocity.z) < 1.0e-3F &&
           DeepRun::Physics::PhysicsQuaternion::SameRotation(after->orientation, initial->orientation);
}

bool G2AsternProducesNegativeResponse()
{
    constexpr float Mass = 1'200'000.0F;
    constexpr float Density = 1025.0F;
    DeepRun::Diagnostics::Logger logger;
    DeepRun::Physics::PhysicsWorld world(logger);
    const auto water = WaterBody::Create({.surfaceLevelY = 0.0F, .densityKgPerCubicMeter = Density});
    if (!world.Initialize() || !water)
    {
        return false;
    }
    const auto handle = world.CreateDynamicBoxBody(E3BodyInfo(Mass));
    const auto initial = world.GetBodyState(handle);
    const BuoyancyComponent buoyancy = E3NeutralBuoyancy(Mass, Density);
    const PropulsionCommand astern{.requestedDriveFraction = -1.0F, .availablePowerFraction = 1.0F};
    PropulsionState propulsion{};
    G2TickSample sample;
    for (int step = 0; step < 360; ++step)
    {
        if (!G2ApplyMarineTick(world, handle, *water, buoyancy, astern, &propulsion, &sample))
        {
            return false;
        }
    }
    const auto after = world.GetBodyState(handle);
    std::cout << "[G2 evidence] astern: RPM " << sample.shaftRpm << ", thrust " << sample.thrustNewtons
              << " N, Vx " << (after ? after->linearVelocity.x : 0.0F) << '\n';
    return initial && after && sample.shaftRpm == -G2Component.maxReverseRpm &&
           sample.thrustNewtons == -G2Component.maxReverseThrustNewtons &&
           after->linearVelocity.x < 0.0F && after->position.x < initial->position.x &&
           std::abs(after->position.y - initial->position.y) < 0.25F;
}

// ---------------------------------------------------------------------------
// M2 Slice H2: compose two published H1 force/point results through the public PhysicsWorld API. These
// headless gates prove physical pitch/depth response; no player command, actuator, animation, or new API.
// ---------------------------------------------------------------------------

namespace
{
constexpr float H2MassKg = 12'000'000.0F;
constexpr float H2DensityKgPerCubicMeter = 1025.0F;
constexpr std::array<ControlSurfaceComponent, 2> H2ControlSurfaces{{
    {.bodyLocalPositionMeters = {32.0F, 0.0F, 0.0F},
     .maxEffectiveLiftAreaSquareMeters = 40.0F},
    {.bodyLocalPositionMeters = {-32.0F, 0.0F, 0.0F},
     .maxEffectiveLiftAreaSquareMeters = 40.0F}}};
constexpr std::array<float, 2> H2DiveDeflections{-0.5F, 0.5F};
constexpr std::array<float, 2> H2RiseDeflections{0.5F, -0.5F};
constexpr std::array<float, 2> H2NeutralDeflections{0.0F, 0.0F};

BuoyancyComponent H2NeutralBuoyancy()
{
    const float pointVolume = (H2MassKg / H2DensityKgPerCubicMeter) / 4.0F;
    return BuoyancyComponent{
        .points = {E2Point({36.0F, 2.0F, 0.0F}, pointVolume, 6.0F),
                   E2Point({12.0F, 2.0F, 0.0F}, pointVolume, 6.0F),
                   E2Point({-12.0F, 2.0F, 0.0F}, pointVolume, 6.0F),
                   E2Point({-36.0F, 2.0F, 0.0F}, pointVolume, 6.0F)}};
}

DeepRun::Physics::DynamicBoxBodyCreateInfo H2BodyInfo(const float forwardSpeed = 0.0F)
{
    DeepRun::Physics::DynamicBoxBodyCreateInfo info;
    info.halfExtents = {51.0F, 6.0F, 6.0F};
    info.mass = H2MassKg;
    info.position = {0.0F, -100.0F, 0.0F};
    info.orientation = {};
    info.gravityEnabled = true;
    info.linearDamping = 0.0F;
    info.angularDamping = 0.0F;
    info.initialLinearVelocity = {forwardSpeed, 0.0F, 0.0F};
    info.initialAngularVelocity = {};
    info.degreesOfFreedom.translationX = true;
    info.degreesOfFreedom.translationY = true;
    info.degreesOfFreedom.translationZ = false;
    info.degreesOfFreedom.rotationX = false;
    info.degreesOfFreedom.rotationY = false;
    info.degreesOfFreedom.rotationZ = true;
    return info;
}

float H2PitchRadians(const PhysicsQuaternion& orientation)
{
    return 2.0F * std::atan2(orientation.z, orientation.w);
}

struct H2TickSample final
{
    PhysicsBodyState before{};
    PhysicsBodyState after{};
    std::array<ControlSurfaceResult, 2> controls{};
    PhysicsVector3 propulsionForceWorld{};
    float shaftRpm = 0.0F;
    float thrustNewtons = 0.0F;
};

bool H2ApplyMarineTick(
    DeepRun::Physics::PhysicsWorld& world,
    const DeepRun::Physics::PhysicsBodyHandle handle,
    const WaterBody& water,
    const BuoyancyComponent& buoyancy,
    const PropulsionCommand& propulsionCommand,
    const std::array<float, 2>& deflections,
    PropulsionState* propulsionState,
    H2TickSample* sample = nullptr)
{
    const auto state = world.GetBodyState(handle); // exactly one beginning-of-tick snapshot
    const auto gravity = world.Gravity();
    if (!state || !gravity || !gravity->IsFinite())
    {
        return false;
    }
    const double gravityMagnitude = std::sqrt(
        static_cast<double>(gravity->x) * gravity->x + static_cast<double>(gravity->y) * gravity->y +
        static_cast<double>(gravity->z) * gravity->z);
    const auto buoyancyResult = BuoyancySystem::Calculate(
        water,
        buoyancy,
        {.worldPositionMeters = state->position, .worldOrientation = state->orientation},
        static_cast<float>(gravityMagnitude));
    const auto dragResult = HydroDragSystem::Calculate(
        water,
        G2Drag,
        {.worldOrientation = state->orientation,
         .worldLinearVelocityMetersPerSecond = state->linearVelocity,
         .worldAngularVelocityRadiansPerSecond = state->angularVelocity});
    const auto propulsionResult = PropulsionSystem::Advance(
        G2Component, *propulsionState, propulsionCommand, E3FixedDeltaSeconds);

    const ControlSurfaceKinematics controlKinematics{
        .bodyWorldPositionMeters = state->position,
        .worldOrientation = state->orientation,
        .worldLinearVelocityMetersPerSecond = state->linearVelocity};
    std::array<std::expected<ControlSurfaceResult, ControlSurfaceError>, 2> controlResults{
        ControlSurfaceSystem::Calculate(water, H2ControlSurfaces[0], controlKinematics, deflections[0]),
        ControlSurfaceSystem::Calculate(water, H2ControlSurfaces[1], controlKinematics, deflections[1])};
    if (!buoyancyResult || !dragResult || !propulsionResult || !controlResults[0] || !controlResults[1])
    {
        return false;
    }

    const auto propulsionForce = DeepRun::Game::RotateBodyLocalVectorToWorld(
        state->orientation, {propulsionResult->thrustNewtons, 0.0F, 0.0F});
    const auto propulsorPoint = DeepRun::Game::TransformBodyLocalPointToWorld(
        state->position, state->orientation, G2PropulsorBodyLocal);
    if (!propulsionForce || !propulsorPoint)
    {
        return false;
    }

    for (const auto& point : buoyancyResult->points)
    {
        if (!world.AddForceAtWorldPosition(handle, point.forceNewtons, point.worldPositionMeters))
        {
            return false;
        }
    }
    if (!world.AddForceAtWorldPosition(handle, dragResult->forceNewtons, state->position) ||
        !world.AddTorque(handle, dragResult->torqueNewtonMeters) ||
        !world.AddForceAtWorldPosition(handle, *propulsionForce, *propulsorPoint) ||
        !world.AddForceAtWorldPosition(
            handle, controlResults[0]->forceNewtons, controlResults[0]->worldPositionMeters) ||
        !world.AddForceAtWorldPosition(
            handle, controlResults[1]->forceNewtons, controlResults[1]->worldPositionMeters))
    {
        return false;
    }

    *propulsionState = propulsionResult->nextState;
    world.Step(E3FixedDeltaSeconds);
    const auto after = world.GetBodyState(handle);
    if (!after)
    {
        return false;
    }
    if (sample != nullptr)
    {
        *sample = {
            .before = *state,
            .after = *after,
            .controls = {*controlResults[0], *controlResults[1]},
            .propulsionForceWorld = *propulsionForce,
            .shaftRpm = propulsionResult->nextState.shaftRpm,
            .thrustNewtons = propulsionResult->thrustNewtons};
    }
    return true;
}

bool H2PlanarState(const PhysicsBodyState& state)
{
    return std::abs(state.position.z) < 1.0e-5F && std::abs(state.linearVelocity.z) < 1.0e-5F &&
           std::abs(state.angularVelocity.x) < 1.0e-5F && std::abs(state.angularVelocity.y) < 1.0e-5F;
}
} // namespace

bool H2ZeroSpeedHasNoPhysicalPitchResponse()
{
    DeepRun::Diagnostics::Logger logger;
    DeepRun::Physics::PhysicsWorld world(logger);
    const auto water = WaterBody::Create(
        {.surfaceLevelY = 0.0F, .densityKgPerCubicMeter = H2DensityKgPerCubicMeter});
    if (!world.Initialize() || !water)
    {
        return false;
    }
    const auto handle = world.CreateDynamicBoxBody(H2BodyInfo());
    const auto initial = world.GetBodyState(handle);
    const BuoyancyComponent buoyancy = H2NeutralBuoyancy();
    const PropulsionCommand idle{.requestedDriveFraction = 0.0F, .availablePowerFraction = 1.0F};
    PropulsionState propulsion{};
    H2TickSample sample;
    for (int step = 0; step < 120; ++step)
    {
        if (!H2ApplyMarineTick(
                world, handle, *water, buoyancy, idle, H2DiveDeflections, &propulsion, &sample))
        {
            return false;
        }
    }
    std::cout << "[H2 evidence] zero speed: bow force " << sample.controls[0].forceNewtons.y
              << " N, stern force " << sample.controls[1].forceNewtons.y << " N, omegaZ "
              << sample.after.angularVelocity.z << ", pitch " << H2PitchRadians(sample.after.orientation)
              << " rad\n";
    return initial && sample.controls[0].forceNewtons == PhysicsVector3{} &&
           sample.controls[1].forceNewtons == PhysicsVector3{} &&
           std::abs(sample.after.angularVelocity.z) < 1.0e-5F &&
           std::abs(H2PitchRadians(sample.after.orientation)) < 1.0e-5F &&
           std::abs(sample.after.position.y - initial->position.y) < 0.01F && H2PlanarState(sample.after);
}

bool H2SymmetricPairCreatesNegativePitchWithoutNetLift()
{
    DeepRun::Diagnostics::Logger logger;
    DeepRun::Physics::PhysicsWorld world(logger);
    const auto water = WaterBody::Create(
        {.surfaceLevelY = 0.0F, .densityKgPerCubicMeter = H2DensityKgPerCubicMeter});
    if (!world.Initialize() || !water)
    {
        return false;
    }
    const auto handle = world.CreateDynamicBoxBody(H2BodyInfo(8.0F));
    const PropulsionCommand idle{.requestedDriveFraction = 0.0F, .availablePowerFraction = 1.0F};
    PropulsionState propulsion{};
    H2TickSample sample;
    if (!H2ApplyMarineTick(
            world, handle, *water, H2NeutralBuoyancy(), idle, H2DiveDeflections, &propulsion, &sample))
    {
        return false;
    }
    const PhysicsVector3 forceSum{
        sample.controls[0].forceNewtons.x + sample.controls[1].forceNewtons.x,
        sample.controls[0].forceNewtons.y + sample.controls[1].forceNewtons.y,
        sample.controls[0].forceNewtons.z + sample.controls[1].forceNewtons.z};
    std::cout << "[H2 evidence] pair at 8 m/s: bow Y " << sample.controls[0].forceNewtons.y
              << " N @ X " << sample.controls[0].worldPositionMeters.x << ", stern Y "
              << sample.controls[1].forceNewtons.y << " N @ X "
              << sample.controls[1].worldPositionMeters.x << ", sum Y " << forceSum.y
              << " N, omegaZ " << sample.after.angularVelocity.z << '\n';
    return sample.controls[0].forceNewtons.y < 0.0F && sample.controls[1].forceNewtons.y > 0.0F &&
           E2VectorNear(forceSum, {}) && sample.after.angularVelocity.z < -1.0e-6F &&
           std::abs(sample.after.linearVelocity.y) < 1.0e-4F && H2PlanarState(sample.after);
}

bool H2OppositePairCreatesPositivePitch()
{
    DeepRun::Diagnostics::Logger logger;
    DeepRun::Physics::PhysicsWorld world(logger);
    const auto water = WaterBody::Create(
        {.surfaceLevelY = 0.0F, .densityKgPerCubicMeter = H2DensityKgPerCubicMeter});
    if (!world.Initialize() || !water)
    {
        return false;
    }
    const auto handle = world.CreateDynamicBoxBody(H2BodyInfo(8.0F));
    const PropulsionCommand idle{.requestedDriveFraction = 0.0F, .availablePowerFraction = 1.0F};
    PropulsionState propulsion{};
    H2TickSample sample;
    return H2ApplyMarineTick(
               world, handle, *water, H2NeutralBuoyancy(), idle, H2RiseDeflections, &propulsion, &sample) &&
           sample.controls[0].forceNewtons.y > 0.0F && sample.controls[1].forceNewtons.y < 0.0F &&
           sample.after.angularVelocity.z > 1.0e-6F && H2PlanarState(sample.after);
}

bool H2ControlAuthorityGrowsWithSpeed()
{
    auto run = [](const float speed, H2TickSample* sample) {
        DeepRun::Diagnostics::Logger logger;
        DeepRun::Physics::PhysicsWorld world(logger);
        const auto water = WaterBody::Create(
            {.surfaceLevelY = 0.0F, .densityKgPerCubicMeter = H2DensityKgPerCubicMeter});
        if (!world.Initialize() || !water)
        {
            return false;
        }
        const auto handle = world.CreateDynamicBoxBody(H2BodyInfo(speed));
        const PropulsionCommand idle{.requestedDriveFraction = 0.0F, .availablePowerFraction = 1.0F};
        PropulsionState propulsion{};
        return H2ApplyMarineTick(
            world, handle, *water, H2NeutralBuoyancy(), idle, H2DiveDeflections, &propulsion, sample);
    };
    H2TickSample slow;
    H2TickSample fast;
    if (!run(4.0F, &slow) || !run(8.0F, &fast))
    {
        return false;
    }
    const float forceRatio = std::abs(fast.controls[0].forceNewtons.y / slow.controls[0].forceNewtons.y);
    const float omegaRatio = std::abs(fast.after.angularVelocity.z / slow.after.angularVelocity.z);
    std::cout << "[H2 evidence] authority: 4 m/s force " << slow.controls[0].forceNewtons.y
              << " N, omegaZ " << slow.after.angularVelocity.z << "; 8 m/s force "
              << fast.controls[0].forceNewtons.y << " N, omegaZ " << fast.after.angularVelocity.z << '\n';
    return E2Near(forceRatio, 4.0F) && omegaRatio > 3.0F && fast.after.angularVelocity.z < 0.0F &&
           slow.after.angularVelocity.z < 0.0F;
}

bool H2PropulsionPitchDepthAndControlRelease()
{
    constexpr int DiveSteps = 3600;    // 60 seconds, including the six-second shaft spin-up
    constexpr int ReleaseSteps = 5400; // 90 seconds for physical restoring/damping to settle at a new depth
    DeepRun::Diagnostics::Logger logger;
    DeepRun::Physics::PhysicsWorld world(logger);
    const auto water = WaterBody::Create(
        {.surfaceLevelY = 0.0F, .densityKgPerCubicMeter = H2DensityKgPerCubicMeter});
    if (!world.Initialize() || !water)
    {
        return false;
    }
    const auto handle = world.CreateDynamicBoxBody(H2BodyInfo());
    const auto initial = world.GetBodyState(handle);
    if (!initial)
    {
        return false;
    }
    const auto initialWater = water->Sample(initial->position);
    const BuoyancyComponent buoyancy = H2NeutralBuoyancy();
    const PropulsionCommand ahead{.requestedDriveFraction = 1.0F, .availablePowerFraction = 1.0F};
    PropulsionState propulsion{};
    H2TickSample sample;
    float minimumPitch = 0.0F;
    float minimumVerticalVelocity = 0.0F;
    float minimumThrustY = 0.0F;
    float maximumControlForce = 0.0F;
    for (int step = 0; step < DiveSteps; ++step)
    {
        if (!H2ApplyMarineTick(
                world, handle, *water, buoyancy, ahead, H2DiveDeflections, &propulsion, &sample))
        {
            return false;
        }
        minimumPitch = (std::min)(minimumPitch, H2PitchRadians(sample.after.orientation));
        minimumVerticalVelocity = (std::min)(minimumVerticalVelocity, sample.after.linearVelocity.y);
        minimumThrustY = (std::min)(minimumThrustY, sample.propulsionForceWorld.y);
        maximumControlForce = (std::max)(maximumControlForce, std::abs(sample.controls[0].forceNewtons.y));
        if (!H2PlanarState(sample.after))
        {
            return false;
        }
    }
    const auto diveWater = water->Sample(sample.after.position);
    const float divePitch = H2PitchRadians(sample.after.orientation);
    const float diveDepth = diveWater ? diveWater->signedDepthMeters : 0.0F;
    const float diveVelocityX = sample.after.linearVelocity.x;
    const float diveVelocityY = sample.after.linearVelocity.y;
    const float diveOmegaZ = sample.after.angularVelocity.z;
    const float diveRpm = sample.shaftRpm;

    bool releasedForcesAreZero = false;
    for (int step = 0; step < ReleaseSteps; ++step)
    {
        if (!H2ApplyMarineTick(
                world, handle, *water, buoyancy, ahead, H2NeutralDeflections, &propulsion, &sample))
        {
            return false;
        }
        if (step == 0)
        {
            releasedForcesAreZero = sample.controls[0].forceNewtons == PhysicsVector3{} &&
                                    sample.controls[1].forceNewtons == PhysicsVector3{};
        }
        if (!H2PlanarState(sample.after))
        {
            return false;
        }
    }
    const auto finalWater = water->Sample(sample.after.position);
    if (!initialWater || !diveWater || !finalWater)
    {
        return false;
    }
    std::cout << "[H2 evidence] 60 s dive: RPM " << diveRpm << ", V (" << diveVelocityX << ','
              << diveVelocityY << "), pitch " << divePitch << " rad, omegaZ " << diveOmegaZ
              << " rad/s, min pitch " << minimumPitch
              << ", thrustY min " << minimumThrustY << " N, control |Fy|max " << maximumControlForce
              << " N, depth " << initialWater->signedDepthMeters << " -> " << diveDepth
              << " m; after 90 s release depth " << finalWater->signedDepthMeters << ", pitch "
              << H2PitchRadians(sample.after.orientation) << " rad, omegaZ "
              << sample.after.angularVelocity.z << " rad/s\n";
    return diveRpm == G2Component.maxForwardRpm && maximumControlForce > 1.0F && minimumPitch < -0.01F &&
           minimumVerticalVelocity < -0.01F && minimumThrustY < -1.0F &&
           diveDepth > initialWater->signedDepthMeters + 1.0F && releasedForcesAreZero &&
           finalWater->signedDepthMeters > initialWater->signedDepthMeters + 1.0F &&
           std::abs(H2PitchRadians(sample.after.orientation)) < 0.06F &&
           std::abs(sample.after.angularVelocity.z) < 0.01F;
}

// ---------------------------------------------------------------------------
// M2 Slice I1: the direct vessel command is the only source of G1 requested drive and H2 deflections.
// These retain H2's published-force physics path while proving input commands do not bypass it.
// ---------------------------------------------------------------------------

bool I1ApplyVesselCommandTick(
    DeepRun::Physics::PhysicsWorld& world,
    const DeepRun::Physics::PhysicsBodyHandle handle,
    const WaterBody& water,
    const BuoyancyComponent& buoyancy,
    const DeepRun::Game::VesselCommandState& command,
    PropulsionState* propulsionState,
    H2TickSample* sample = nullptr)
{
    if (!DeepRun::Game::ValidateVesselCommandState(command))
    {
        return false;
    }
    const PropulsionCommand propulsionCommand{
        .requestedDriveFraction = command.throttleFraction,
        .availablePowerFraction = 1.0F};
    const std::array<float, 2> deflections{
        -0.5F * command.depthCommandFraction,
        0.5F * command.depthCommandFraction};
    return H2ApplyMarineTick(world, handle, water, buoyancy, propulsionCommand, deflections, propulsionState, sample);
}

bool I1NeutralCommandIsPhysicalDefault()
{
    DeepRun::Diagnostics::Logger logger;
    DeepRun::Physics::PhysicsWorld world(logger);
    const auto water = WaterBody::Create({.surfaceLevelY = 0.0F, .densityKgPerCubicMeter = H2DensityKgPerCubicMeter});
    if (!world.Initialize() || !water)
    {
        return false;
    }
    const auto handle = world.CreateDynamicBoxBody(H2BodyInfo());
    const auto initial = world.GetBodyState(handle);
    PropulsionState propulsion{};
    H2TickSample sample;
    const DeepRun::Game::VesselCommandState neutral{};
    for (int step = 0; step < 120; ++step)
    {
        if (!I1ApplyVesselCommandTick(world, handle, *water, H2NeutralBuoyancy(), neutral, &propulsion, &sample))
        {
            return false;
        }
    }
    return initial && sample.shaftRpm == 0.0F && sample.thrustNewtons == 0.0F &&
           sample.controls[0].forceNewtons == PhysicsVector3{} && sample.controls[1].forceNewtons == PhysicsVector3{} &&
           std::abs(sample.after.position.y - initial->position.y) < 0.01F && H2PlanarState(sample.after);
}

bool I1ThrottleAheadReleaseAndAstern()
{
    const auto run = [](const float throttle, const int steps, H2TickSample* finalSample) {
        DeepRun::Diagnostics::Logger logger;
        DeepRun::Physics::PhysicsWorld world(logger);
        const auto water = WaterBody::Create(
            {.surfaceLevelY = 0.0F, .densityKgPerCubicMeter = H2DensityKgPerCubicMeter});
        if (!world.Initialize() || !water)
        {
            return std::optional<PhysicsBodyState>{};
        }
        const auto handle = world.CreateDynamicBoxBody(H2BodyInfo());
        PropulsionState propulsion{};
        const DeepRun::Game::VesselCommandState command{.throttleFraction = throttle};
        for (int step = 0; step < steps; ++step)
        {
            if (!I1ApplyVesselCommandTick(world, handle, *water, H2NeutralBuoyancy(), command, &propulsion, finalSample))
            {
                return std::optional<PhysicsBodyState>{};
            }
        }
        return world.GetBodyState(handle);
    };

    H2TickSample ahead;
    const auto aheadState = run(1.0F, 360, &ahead);
    H2TickSample astern;
    const auto asternState = run(-1.0F, 360, &astern);
    if (!aheadState || !asternState || ahead.shaftRpm != G2Component.maxForwardRpm || ahead.thrustNewtons <= 0.0F ||
        aheadState->linearVelocity.x <= 0.0F || std::abs(aheadState->position.y + 100.0F) > 0.25F ||
        std::abs(H2PitchRadians(aheadState->orientation)) > 1.0e-3F ||
        ahead.controls[0].forceNewtons != PhysicsVector3{} || ahead.controls[1].forceNewtons != PhysicsVector3{} ||
        astern.shaftRpm != -G2Component.maxReverseRpm || astern.thrustNewtons >= 0.0F ||
        asternState->linearVelocity.x >= 0.0F)
    {
        return false;
    }

    DeepRun::Diagnostics::Logger logger;
    DeepRun::Physics::PhysicsWorld releaseWorld(logger);
    const auto water = WaterBody::Create({.surfaceLevelY = 0.0F, .densityKgPerCubicMeter = H2DensityKgPerCubicMeter});
    if (!releaseWorld.Initialize() || !water)
    {
        return false;
    }
    const auto handle = releaseWorld.CreateDynamicBoxBody(H2BodyInfo());
    PropulsionState propulsion{};
    H2TickSample release;
    for (int step = 0; step < 360; ++step)
    {
        if (!I1ApplyVesselCommandTick(
                releaseWorld, handle, *water, H2NeutralBuoyancy(), {.throttleFraction = 1.0F}, &propulsion, &release))
        {
            return false;
        }
    }
    const float rpmAtRelease = release.shaftRpm;
    const float thrustAtRelease = release.thrustNewtons;
    for (int step = 0; step < 60; ++step)
    {
        if (!I1ApplyVesselCommandTick(releaseWorld, handle, *water, H2NeutralBuoyancy(), {}, &propulsion, &release))
        {
            return false;
        }
    }
    const float spinDownRpm = release.shaftRpm;
    const float spinDownThrust = release.thrustNewtons;
    for (int step = 0; step < 300; ++step)
    {
        if (!I1ApplyVesselCommandTick(releaseWorld, handle, *water, H2NeutralBuoyancy(), {}, &propulsion, &release))
        {
            return false;
        }
    }
    std::cout << "[I1 evidence] release: RPM " << rpmAtRelease << " -> " << spinDownRpm << " -> "
              << release.shaftRpm << ", thrust " << thrustAtRelease << " -> " << spinDownThrust << " -> "
              << release.thrustNewtons << " N, coast Vx " << release.after.linearVelocity.x << '\n';
    return spinDownRpm > 0.0F && spinDownRpm < rpmAtRelease && spinDownThrust > 0.0F &&
           spinDownThrust < thrustAtRelease && release.shaftRpm == 0.0F && release.thrustNewtons == 0.0F &&
           release.after.linearVelocity.x > 0.0F;
}

bool I1DepthCommandMapsToPhysicalDiveAndSurface()
{
    struct ManeuverEvidence final
    {
        float finalDepth = 0.0F;
        float extremePitch = 0.0F;
        float extremeWorldThrustY = 0.0F;
        float extremeOmegaZ = 0.0F;
        float extremeBowForceY = 0.0F;
        float extremeSternForceY = 0.0F;
        bool releaseForcesZero = false;
    };
    const auto run = [](const float depth, ManeuverEvidence* evidence) {
        DeepRun::Diagnostics::Logger logger;
        DeepRun::Physics::PhysicsWorld world(logger);
        const auto water = WaterBody::Create(
            {.surfaceLevelY = 0.0F, .densityKgPerCubicMeter = H2DensityKgPerCubicMeter});
        if (!world.Initialize() || !water)
        {
            return false;
        }
        const auto handle = world.CreateDynamicBoxBody(H2BodyInfo());
        PropulsionState propulsion{};
        H2TickSample sample;
        const DeepRun::Game::VesselCommandState maneuver{
            .throttleFraction = 1.0F,
            .depthCommandFraction = depth};
        for (int step = 0; step < 1200; ++step) // 20 seconds; remains well below the flat surface
        {
            if (!I1ApplyVesselCommandTick(world, handle, *water, H2NeutralBuoyancy(), maneuver, &propulsion, &sample))
            {
                return false;
            }
            const float pitch = H2PitchRadians(sample.after.orientation);
            if (depth > 0.0F)
            {
                evidence->extremePitch = (std::min)(evidence->extremePitch, pitch);
                evidence->extremeWorldThrustY = (std::min)(evidence->extremeWorldThrustY, sample.propulsionForceWorld.y);
                evidence->extremeOmegaZ = (std::min)(evidence->extremeOmegaZ, sample.after.angularVelocity.z);
                evidence->extremeBowForceY = (std::min)(evidence->extremeBowForceY, sample.controls[0].forceNewtons.y);
                evidence->extremeSternForceY = (std::max)(evidence->extremeSternForceY, sample.controls[1].forceNewtons.y);
            }
            else
            {
                evidence->extremePitch = (std::max)(evidence->extremePitch, pitch);
                evidence->extremeWorldThrustY = (std::max)(evidence->extremeWorldThrustY, sample.propulsionForceWorld.y);
                evidence->extremeOmegaZ = (std::max)(evidence->extremeOmegaZ, sample.after.angularVelocity.z);
                evidence->extremeBowForceY = (std::max)(evidence->extremeBowForceY, sample.controls[0].forceNewtons.y);
                evidence->extremeSternForceY = (std::min)(evidence->extremeSternForceY, sample.controls[1].forceNewtons.y);
            }
        }
        const auto waterSample = water->Sample(sample.after.position);
        if (!waterSample || !I1ApplyVesselCommandTick(
                                world,
                                handle,
                                *water,
                                H2NeutralBuoyancy(),
                                {.throttleFraction = 1.0F},
                                &propulsion,
                                &sample))
        {
            return false;
        }
        evidence->finalDepth = waterSample->signedDepthMeters;
        evidence->releaseForcesZero = sample.controls[0].forceNewtons == PhysicsVector3{} &&
                                      sample.controls[1].forceNewtons == PhysicsVector3{};
        return true;
    };

    ManeuverEvidence dive;
    ManeuverEvidence surface;
    if (!run(1.0F, &dive) || !run(-1.0F, &surface))
    {
        return false;
    }
    std::cout << "[I1 evidence] Depth +1: depth 100 -> " << dive.finalDepth << " m, min pitch "
              << dive.extremePitch << " rad, min thrustY " << dive.extremeWorldThrustY
              << " N; Depth -1: depth 100 -> " << surface.finalDepth << " m, max pitch "
              << surface.extremePitch << " rad, max thrustY " << surface.extremeWorldThrustY << " N\n";
    return dive.extremeBowForceY < -1.0F && dive.extremeSternForceY > 1.0F && dive.extremeOmegaZ < -1.0e-6F &&
           dive.extremePitch < -0.01F && dive.extremeWorldThrustY < -1.0F &&
           dive.finalDepth > 100.5F && dive.releaseForcesZero && surface.extremeBowForceY > 1.0F &&
           surface.extremeSternForceY < -1.0F &&
           surface.extremeOmegaZ > 1.0e-6F &&
           surface.extremePitch > 0.01F && surface.extremeWorldThrustY > 1.0F && surface.finalDepth < 99.5F &&
           surface.finalDepth > 20.0F && surface.releaseForcesZero;
}

bool I1DepthReleaseAndLowSpeedHaveNoMagicAuthority()
{
    const auto water = WaterBody::Create({.surfaceLevelY = 0.0F, .densityKgPerCubicMeter = H2DensityKgPerCubicMeter});
    if (!water)
    {
        return false;
    }
    bool zeroSpeedNoAuthority = false;
    {
        DeepRun::Diagnostics::Logger logger;
        DeepRun::Physics::PhysicsWorld world(logger);
        if (!world.Initialize())
        {
            return false;
        }
        const auto handle = world.CreateDynamicBoxBody(H2BodyInfo());
        const auto initial = world.GetBodyState(handle);
        PropulsionState propulsion{};
        H2TickSample sample;
        for (int step = 0; step < 120; ++step)
        {
            if (!I1ApplyVesselCommandTick(
                    world,
                    handle,
                    *water,
                    H2NeutralBuoyancy(),
                    {.depthCommandFraction = 1.0F},
                    &propulsion,
                    &sample))
            {
                return false;
            }
        }
        zeroSpeedNoAuthority = initial && sample.controls[0].forceNewtons == PhysicsVector3{} &&
                               sample.controls[1].forceNewtons == PhysicsVector3{} &&
                               std::abs(sample.after.angularVelocity.z) < 1.0e-5F &&
                               std::abs(sample.after.position.y - initial->position.y) < 0.01F;
    }

    DeepRun::Diagnostics::Logger logger;
    DeepRun::Physics::PhysicsWorld movingWorld(logger);
    if (!movingWorld.Initialize())
    {
        return false;
    }
    H2TickSample sample;
    const auto movingHandle = movingWorld.CreateDynamicBoxBody(H2BodyInfo(8.0F));
    PropulsionState movingPropulsion{};
    if (!I1ApplyVesselCommandTick(
            movingWorld,
            movingHandle,
            *water,
            H2NeutralBuoyancy(),
            {.depthCommandFraction = 1.0F},
            &movingPropulsion,
            &sample) ||
        !I1ApplyVesselCommandTick(movingWorld, movingHandle, *water, H2NeutralBuoyancy(), {}, &movingPropulsion, &sample))
    {
        return false;
    }
    return zeroSpeedNoAuthority && sample.controls[0].forceNewtons == PhysicsVector3{} &&
           sample.controls[1].forceNewtons == PhysicsVector3{};
}

bool I1CommandSnapshotIsFixedStepStable()
{
    DeepRun::Diagnostics::Logger logger;
    DeepRun::Physics::PhysicsWorld world(logger);
    const auto water = WaterBody::Create({.surfaceLevelY = 0.0F, .densityKgPerCubicMeter = H2DensityKgPerCubicMeter});
    if (!world.Initialize() || !water)
    {
        return false;
    }
    const auto handle = world.CreateDynamicBoxBody(H2BodyInfo());
    const DeepRun::Game::VesselCommandState command{.throttleFraction = 1.0F, .depthCommandFraction = 0.0F};
    PropulsionState propulsion{};
    H2TickSample sample;
    for (int step = 0; step < 120; ++step)
    {
        if (!I1ApplyVesselCommandTick(world, handle, *water, H2NeutralBuoyancy(), command, &propulsion, &sample))
        {
            return false;
        }
    }
    return std::abs(sample.shaftRpm - 60.0F) < 0.01F && sample.controls[0].forceNewtons == PhysicsVector3{} &&
           sample.controls[1].forceNewtons == PhysicsVector3{};
}

// ---------------------------------------------------------------------------
// M2 Slice I2: Game semantic engine feedback, generic presentation-time mixing, and pure backend
// conversion. No test calls controller hardware and no haptic state is an input to simulation.
// ---------------------------------------------------------------------------

bool I2MixerEmptyIsSilent()
{
    const DeepRun::Input::HapticMixer mixer;
    return mixer.CurrentOutput() == DeepRun::Input::GamepadVibration{};
}

bool I2MixerSingleEffect()
{
    DeepRun::Input::HapticMixer mixer;
    const auto submitted = mixer.Submit({
        .id = 1,
        .lowFrequencyMotor = 0.4F,
        .highFrequencyMotor = 0.2F,
        .durationSeconds = 1.0F,
        .priority = 10});
    const auto output = mixer.CurrentOutput();
    return submitted && E2Near(output.lowFrequencyMotor, 0.4F) &&
           E2Near(output.highFrequencyMotor, 0.2F);
}

bool I2MixerSameIdReplacesAndRefreshes()
{
    DeepRun::Input::HapticMixer mixer;
    const auto first = mixer.Submit({
        .id = 1,
        .lowFrequencyMotor = 0.2F,
        .durationSeconds = 0.1F,
        .priority = 10});
    const auto firstAdvance = mixer.Advance(0.05F);
    const auto refreshed = mixer.Submit({
        .id = 1,
        .lowFrequencyMotor = 0.6F,
        .durationSeconds = 0.1F,
        .priority = 10});
    const auto immediate = mixer.CurrentOutput();
    const auto refreshedAdvance = mixer.Advance(0.06F);
    const auto afterOriginalExpiry = mixer.CurrentOutput();
    const auto zeroRefresh = mixer.Submit({
        .id = 1,
        .lowFrequencyMotor = 0.0F,
        .durationSeconds = 0.1F,
        .priority = 10});
    return first && firstAdvance && refreshed && refreshedAdvance &&
           E2Near(immediate.lowFrequencyMotor, 0.6F) &&
           E2Near(afterOriginalExpiry.lowFrequencyMotor, 0.6F) && zeroRefresh &&
           mixer.CurrentOutput() == DeepRun::Input::GamepadVibration{};
}

bool I2MixerSamePriorityAddsAndClamps()
{
    DeepRun::Input::HapticMixer mixer;
    const auto first = mixer.Submit({
        .id = 1,
        .lowFrequencyMotor = 0.6F,
        .highFrequencyMotor = 0.2F,
        .durationSeconds = 1.0F,
        .priority = 10});
    const auto second = mixer.Submit({
        .id = 2,
        .lowFrequencyMotor = 0.6F,
        .highFrequencyMotor = 0.9F,
        .durationSeconds = 1.0F,
        .priority = 10});
    const auto output = mixer.CurrentOutput();
    return first && second && output.lowFrequencyMotor == 1.0F && output.highFrequencyMotor == 1.0F;
}

bool I2MixerPrioritySuppressesAndResumes()
{
    DeepRun::Input::HapticMixer mixer;
    const auto low = mixer.Submit({
        .id = 1,
        .lowFrequencyMotor = 0.3F,
        .durationSeconds = 1.0F,
        .priority = 5});
    const auto high = mixer.Submit({
        .id = 2,
        .highFrequencyMotor = 0.8F,
        .durationSeconds = 0.1F,
        .priority = 20});
    const auto overridden = mixer.CurrentOutput();
    const auto advanced = mixer.Advance(0.11F);
    const auto resumed = mixer.CurrentOutput();
    return low && high && advanced && overridden.lowFrequencyMotor == 0.0F &&
           E2Near(overridden.highFrequencyMotor, 0.8F) && E2Near(resumed.lowFrequencyMotor, 0.3F) &&
           resumed.highFrequencyMotor == 0.0F;
}

bool I2MixerDurationExpiresDeterministically()
{
    DeepRun::Input::HapticMixer mixer;
    const auto submitted = mixer.Submit({
        .id = 1,
        .lowFrequencyMotor = 0.4F,
        .durationSeconds = 0.1F,
        .priority = 10});
    const auto partialAdvance = mixer.Advance(0.04F);
    const auto active = mixer.CurrentOutput();
    const auto expiryAdvance = mixer.Advance(0.061F);
    return submitted && partialAdvance && expiryAdvance && E2Near(active.lowFrequencyMotor, 0.4F) &&
           mixer.CurrentOutput() == DeepRun::Input::GamepadVibration{};
}

bool I2LongCurrentFrameSubmissionSurvivesFirstOutput()
{
    constexpr DeepRun::Input::HapticEffectId StableEffectId = 77;
    DeepRun::Input::HapticMixer mixer;
    const auto inheritedEffectsAged = mixer.Advance(0.15F);
    const auto currentFixedPhaseSubmission = mixer.Submit({
        .id = StableEffectId,
        .lowFrequencyMotor = 0.5F,
        .durationSeconds = 0.10F,
        .priority = 10});
    const auto currentFrameOutput = mixer.CurrentOutput();
    std::cout << "[I2 corrective evidence] long frame 0.15 s, current 0.10 s effect output low "
              << currentFrameOutput.lowFrequencyMotor << '\n';
    return inheritedEffectsAged && currentFixedPhaseSubmission &&
           E2Near(currentFrameOutput.lowFrequencyMotor, 0.5F) &&
           currentFrameOutput.highFrequencyMotor == 0.0F;
}

bool I2OldUnrefreshedEffectExpiresOnLongFrame()
{
    DeepRun::Input::HapticMixer mixer;
    const auto priorFrameSubmission = mixer.Submit({
        .id = 77,
        .lowFrequencyMotor = 0.5F,
        .durationSeconds = 0.10F,
        .priority = 10});
    const auto nextFrameAdvance = mixer.Advance(0.15F);
    const auto output = mixer.CurrentOutput();
    std::cout << "[I2 corrective evidence] prior 0.10 s effect after unrefreshed 0.15 s frame low "
              << output.lowFrequencyMotor << '\n';
    return priorFrameSubmission && nextFrameAdvance && output == DeepRun::Input::GamepadVibration{};
}

bool I2SameIdRefreshAfterLongFrameExpiryDoesNotStack()
{
    DeepRun::Input::HapticMixer mixer;
    const auto priorFrameSubmission = mixer.Submit({
        .id = 77,
        .lowFrequencyMotor = 0.2F,
        .durationSeconds = 0.10F,
        .priority = 10});
    const auto nextFrameAdvance = mixer.Advance(0.15F);
    const auto currentFixedPhaseRefresh = mixer.Submit({
        .id = 77,
        .lowFrequencyMotor = 0.7F,
        .durationSeconds = 0.10F,
        .priority = 10});
    const auto output = mixer.CurrentOutput();
    std::cout << "[I2 corrective evidence] expired ID refreshed at low 0.7, resolved low "
              << output.lowFrequencyMotor << '\n';
    return priorFrameSubmission && nextFrameAdvance && currentFixedPhaseRefresh &&
           E2Near(output.lowFrequencyMotor, 0.7F) && output.highFrequencyMotor == 0.0F;
}

bool I2NormalFrameContinuousRefreshIsStable()
{
    DeepRun::Input::HapticMixer mixer;
    for (int frame = 0; frame < 120; ++frame)
    {
        if (!mixer.Advance(1.0F / 60.0F) ||
            !mixer.Submit({
                .id = 77,
                .lowFrequencyMotor = 0.35F,
                .highFrequencyMotor = 0.05F,
                .durationSeconds = 0.10F,
                .priority = 10}))
        {
            return false;
        }
        const auto output = mixer.CurrentOutput();
        if (!E2Near(output.lowFrequencyMotor, 0.35F) || !E2Near(output.highFrequencyMotor, 0.05F))
        {
            return false;
        }
    }
    std::cout << "[I2 corrective evidence] 120 frames at 60 Hz remained 0.35/0.05\n";
    return true;
}

bool I2MixerMasterIntensity()
{
    DeepRun::Input::HapticMixer mixer;
    const auto submitted = mixer.Submit({
        .id = 1,
        .lowFrequencyMotor = 0.8F,
        .highFrequencyMotor = 0.4F,
        .durationSeconds = 1.0F,
        .priority = 10});
    const auto master = mixer.SetMasterIntensity(0.5F);
    const auto single = mixer.CurrentOutput();
    const auto second = mixer.Submit({
        .id = 2,
        .lowFrequencyMotor = 0.8F,
        .highFrequencyMotor = 0.8F,
        .durationSeconds = 1.0F,
        .priority = 10});
    const auto clampedThenScaled = mixer.CurrentOutput();
    return submitted && master && second && E2Near(single.lowFrequencyMotor, 0.4F) &&
           E2Near(single.highFrequencyMotor, 0.2F) &&
           E2Near(clampedThenScaled.lowFrequencyMotor, 0.5F) &&
           E2Near(clampedThenScaled.highFrequencyMotor, 0.5F);
}

bool I2MixerDisableKeepsAgeing()
{
    DeepRun::Input::HapticMixer mixer;
    const auto submitted = mixer.Submit({
        .id = 1,
        .lowFrequencyMotor = 0.8F,
        .highFrequencyMotor = 0.4F,
        .durationSeconds = 0.1F,
        .priority = 10});
    mixer.SetEnabled(false);
    const bool suppressed = mixer.CurrentOutput() == DeepRun::Input::GamepadVibration{};
    const auto advanced = mixer.Advance(0.11F);
    mixer.SetEnabled(true);
    return submitted && suppressed && advanced && mixer.Enabled() &&
           mixer.CurrentOutput() == DeepRun::Input::GamepadVibration{};
}

bool I2MixerRejectsMalformedConfiguration()
{
    using DeepRun::Input::HapticEffectRequest;
    DeepRun::Input::HapticMixer mixer;
    const HapticEffectRequest valid{
        .id = 1,
        .lowFrequencyMotor = 0.2F,
        .highFrequencyMotor = 0.3F,
        .durationSeconds = 1.0F,
        .priority = 10};
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    auto rejected = [&mixer, &valid](auto mutate) {
        HapticEffectRequest request = valid;
        mutate(request);
        return !mixer.Submit(request);
    };
    return rejected([](auto& request) { request.id = DeepRun::Input::InvalidHapticEffectId; }) &&
           rejected([nan](auto& request) { request.lowFrequencyMotor = nan; }) &&
           rejected([infinity](auto& request) { request.lowFrequencyMotor = infinity; }) &&
           rejected([](auto& request) { request.lowFrequencyMotor = -0.1F; }) &&
           rejected([](auto& request) { request.highFrequencyMotor = 1.1F; }) &&
           rejected([](auto& request) { request.durationSeconds = 0.0F; }) &&
           rejected([](auto& request) { request.durationSeconds = -1.0F; }) &&
           rejected([nan](auto& request) { request.durationSeconds = nan; }) &&
           rejected([infinity](auto& request) { request.durationSeconds = infinity; }) &&
           !mixer.SetMasterIntensity(nan) && !mixer.SetMasterIntensity(infinity) &&
           !mixer.SetMasterIntensity(-0.1F) && !mixer.SetMasterIntensity(1.1F) &&
           !mixer.Advance(-0.1F) && !mixer.Advance(nan);
}

bool WgiVibrationConversionUsesNormalizedMotors()
{
    using DeepRun::Input::GamepadVibration;
    using DeepRun::Input::Windows::MapWindowsGamepadVibration;
    const auto active = MapWindowsGamepadVibration({.lowFrequencyMotor = 0.55F, .highFrequencyMotor = 0.10F});
    const auto zero = MapWindowsGamepadVibration(GamepadVibration{});
    return std::abs(active.leftMotor - 0.55) < 1.0e-6 && std::abs(active.rightMotor - 0.10) < 1.0e-6 &&
           active.leftTrigger == 0.0 && active.rightTrigger == 0.0 && zero.leftMotor == 0.0 &&
           zero.rightMotor == 0.0 && zero.leftTrigger == 0.0 && zero.rightTrigger == 0.0;
}

bool I2SemanticEngineVibrationMapping()
{
    const DeepRun::Game::HapticFeedbackSystem feedback;
    const auto zero = feedback.Map({.type = DeepRun::Game::HapticEventType::EngineVibration, .intensity = 0.0F});
    const auto mid = feedback.Map({.type = DeepRun::Game::HapticEventType::EngineVibration, .intensity = 0.5F});
    const auto full = feedback.Map({.type = DeepRun::Game::HapticEventType::EngineVibration, .intensity = 1.0F});
    const auto nan = feedback.Map({
        .type = DeepRun::Game::HapticEventType::EngineVibration,
        .intensity = std::numeric_limits<float>::quiet_NaN()});
    const auto below = feedback.Map({.type = DeepRun::Game::HapticEventType::EngineVibration, .intensity = -0.1F});
    const auto above = feedback.Map({.type = DeepRun::Game::HapticEventType::EngineVibration, .intensity = 1.1F});
    return zero && mid && full && zero->id == DeepRun::Game::HapticFeedbackSystem::EngineVibrationEffectId &&
           zero->lowFrequencyMotor == 0.0F && zero->highFrequencyMotor == 0.0F &&
           E2Near(mid->lowFrequencyMotor, 0.275F) && E2Near(mid->highFrequencyMotor, 0.05F) &&
           full->id == zero->id && E2Near(full->lowFrequencyMotor, 0.55F) &&
           E2Near(full->highFrequencyMotor, 0.10F) && E2Near(full->durationSeconds, 0.10F) &&
           full->priority == 10 && !nan && !below && !above;
}

bool I2RpmNormalizationUsesAuthoritativeDirectionLimits()
{
    const DeepRun::Game::HapticFeedbackSystem feedback;
    const auto stopped = feedback.EngineVibrationFromShaftRpm(G2Component, {.shaftRpm = 0.0F});
    const auto negativeZero = feedback.EngineVibrationFromShaftRpm(G2Component, {.shaftRpm = -0.0F});
    const auto halfAhead = feedback.EngineVibrationFromShaftRpm(G2Component, {.shaftRpm = 90.0F});
    const auto fullAhead = feedback.EngineVibrationFromShaftRpm(G2Component, {.shaftRpm = 180.0F});
    const auto halfAstern = feedback.EngineVibrationFromShaftRpm(G2Component, {.shaftRpm = -60.0F});
    const auto fullAstern = feedback.EngineVibrationFromShaftRpm(G2Component, {.shaftRpm = -120.0F});
    PropulsionComponent badConfig = G2Component;
    badConfig.maxReverseRpm = 0.0F;
    return stopped && negativeZero && halfAhead && fullAhead && halfAstern && fullAstern &&
           stopped->intensity == 0.0F && negativeZero->intensity == 0.0F &&
           !std::signbit(negativeZero->intensity) &&
           E2Near(halfAhead->intensity, 0.5F) && fullAhead->intensity == 1.0F &&
           E2Near(halfAstern->intensity, 0.5F) && fullAstern->intensity == 1.0F &&
           !feedback.EngineVibrationFromShaftRpm(badConfig, {}) &&
           !feedback.EngineVibrationFromShaftRpm(G2Component, {.shaftRpm = 181.0F}) &&
           !feedback.EngineVibrationFromShaftRpm(
               G2Component, {.shaftRpm = std::numeric_limits<float>::quiet_NaN()});
}

bool I2RpmRampProducesHapticRamp()
{
    const DeepRun::Game::HapticFeedbackSystem feedback;
    PropulsionState aheadState{};
    float previousAheadIntensity = 0.0F;
    bool aheadMonotonic = true;
    for (int tick = 0; tick < 120; ++tick)
    {
        const auto propulsion = PropulsionSystem::Advance(
            G2Component,
            aheadState,
            {.requestedDriveFraction = 1.0F, .availablePowerFraction = 1.0F},
            E3FixedDeltaSeconds);
        if (!propulsion)
        {
            return false;
        }
        aheadState = propulsion->nextState;
        const auto event = feedback.EngineVibrationFromShaftRpm(G2Component, aheadState);
        if (!event || event->intensity < previousAheadIntensity)
        {
            aheadMonotonic = false;
            break;
        }
        previousAheadIntensity = event->intensity;
    }
    const float aheadIntensity = previousAheadIntensity;
    const auto aheadEffect = feedback.Map({DeepRun::Game::HapticEventType::EngineVibration, aheadIntensity});

    float previousReleaseIntensity = aheadIntensity;
    bool releaseMonotonic = true;
    for (int tick = 0; tick < 60; ++tick)
    {
        const auto propulsion = PropulsionSystem::Advance(
            G2Component,
            aheadState,
            {.requestedDriveFraction = 0.0F, .availablePowerFraction = 1.0F},
            E3FixedDeltaSeconds);
        if (!propulsion)
        {
            return false;
        }
        aheadState = propulsion->nextState;
        const auto event = feedback.EngineVibrationFromShaftRpm(G2Component, aheadState);
        if (!event || event->intensity > previousReleaseIntensity)
        {
            releaseMonotonic = false;
            break;
        }
        previousReleaseIntensity = event->intensity;
    }
    const auto releaseEffect = feedback.Map(
        {DeepRun::Game::HapticEventType::EngineVibration, previousReleaseIntensity});

    PropulsionState asternState{};
    PropulsionResult astern{};
    for (int tick = 0; tick < 120; ++tick)
    {
        const auto next = PropulsionSystem::Advance(
            G2Component,
            asternState,
            {.requestedDriveFraction = -1.0F, .availablePowerFraction = 1.0F},
            E3FixedDeltaSeconds);
        if (!next)
        {
            return false;
        }
        astern = *next;
        asternState = next->nextState;
    }
    const auto asternEvent = feedback.EngineVibrationFromShaftRpm(G2Component, asternState);
    const auto asternEffect = asternEvent ? feedback.Map(*asternEvent)
                                         : std::expected<DeepRun::Input::HapticEffectRequest, std::string>{
                                               std::unexpected("astern event unavailable")};
    if (aheadEffect && releaseEffect && asternEvent && asternEffect)
    {
        std::cout << "[I2 evidence] ahead RPM " << 60.0F << ", intensity " << aheadIntensity
                  << ", motors " << aheadEffect->lowFrequencyMotor << '/' << aheadEffect->highFrequencyMotor
                  << "; release RPM " << aheadState.shaftRpm << ", intensity " << previousReleaseIntensity
                  << ", motors " << releaseEffect->lowFrequencyMotor << '/' << releaseEffect->highFrequencyMotor
                  << "; astern RPM " << astern.nextState.shaftRpm << ", intensity " << asternEvent->intensity
                  << ", motors " << asternEffect->lowFrequencyMotor << '/' << asternEffect->highFrequencyMotor
                  << '\n';
    }
    return aheadMonotonic && releaseMonotonic && aheadEffect && releaseEffect && asternEvent && asternEffect &&
           aheadIntensity > 0.0F && aheadIntensity < 1.0F && previousReleaseIntensity < aheadIntensity &&
           previousReleaseIntensity > 0.0F && asternState.shaftRpm < 0.0F && asternEvent->intensity > 0.0F &&
           asternEvent->intensity < 1.0F;
}

bool I2HapticsCannotAffectSimulation()
{
    struct Outcome final
    {
        PhysicsBodyState body{};
        PropulsionState propulsion{};
        float thrustNewtons = 0.0F;
        DeepRun::Game::VesselCommandState command{};
    };
    const auto run = [](const bool hapticsEnabled) -> std::optional<Outcome> {
        DeepRun::Diagnostics::Logger logger;
        DeepRun::Physics::PhysicsWorld world(logger);
        const auto water = WaterBody::Create(
            {.surfaceLevelY = 0.0F, .densityKgPerCubicMeter = H2DensityKgPerCubicMeter});
        if (!world.Initialize() || !water)
        {
            return std::nullopt;
        }
        const auto handle = world.CreateDynamicBoxBody(H2BodyInfo());
        const DeepRun::Game::VesselCommandState command{.throttleFraction = 0.75F};
        const DeepRun::Game::HapticFeedbackSystem feedback;
        DeepRun::Input::HapticMixer mixer;
        mixer.SetEnabled(hapticsEnabled);
        PropulsionState propulsion{};
        H2TickSample sample;
        for (int tick = 0; tick < 120; ++tick)
        {
            if (!I1ApplyVesselCommandTick(
                    world, handle, *water, H2NeutralBuoyancy(), command, &propulsion, &sample))
            {
                return std::nullopt;
            }
            const auto event = feedback.EngineVibrationFromShaftRpm(G2Component, propulsion);
            const auto effect = event ? feedback.Map(*event)
                                      : std::expected<DeepRun::Input::HapticEffectRequest, std::string>{
                                            std::unexpected("event unavailable")};
            if (!effect || !mixer.Submit(*effect) || !mixer.Advance(E3FixedDeltaSeconds))
            {
                return std::nullopt;
            }
        }
        const auto body = world.GetBodyState(handle);
        if (!body)
        {
            return std::nullopt;
        }
        return Outcome{.body = *body, .propulsion = propulsion, .thrustNewtons = sample.thrustNewtons, .command = command};
    };

    const auto enabled = run(true);
    const auto disabled = run(false);
    if (!enabled || !disabled)
    {
        return false;
    }
    return enabled->command.throttleFraction == disabled->command.throttleFraction &&
           enabled->command.depthCommandFraction == disabled->command.depthCommandFraction &&
           enabled->propulsion.shaftRpm == disabled->propulsion.shaftRpm &&
           enabled->thrustNewtons == disabled->thrustNewtons &&
           E2VectorNear(enabled->body.position, disabled->body.position) &&
           E2VectorNear(enabled->body.linearVelocity, disabled->body.linearVelocity) &&
           E2VectorNear(enabled->body.angularVelocity, disabled->body.angularVelocity) &&
           E2Near(enabled->body.orientation.x, disabled->body.orientation.x) &&
           E2Near(enabled->body.orientation.y, disabled->body.orientation.y) &&
           E2Near(enabled->body.orientation.z, disabled->body.orientation.z) &&
           E2Near(enabled->body.orientation.w, disabled->body.orientation.w);
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
    // M3-A stores the former SDR-authored values as scene-linear input, so their numeric difference is
    // smaller before the renderer's one SDR encode. They must still be clearly distinct and opaque — no
    // alpha blending is part of the D2 contract.
    const RgbaColor& above = DeepRun::Game::M2AboveWaterBackgroundColor;
    const RgbaColor& below = DeepRun::Game::M2UnderwaterBackgroundColor;
    if (!above.IsFinite() || !below.IsFinite() || above.a != 1.0F || below.a != 1.0F)
    {
        return false;
    }
    const float channelDifference = std::abs(above.r - below.r) + std::abs(above.g - below.g) +
                                    std::abs(above.b - below.b);
    return channelDifference > 0.1F; // clearly different in scene-linear space, without asserting art direction
}

float M3AToneMapScalar(const float sceneLinear) noexcept
{
    const float nonNegative = std::max(sceneLinear, 0.0F);
    return nonNegative / (1.0F + nonNegative);
}

bool M3IFramePercentiles()
{
    using DeepRun::Diagnostics::SummarizeFrameSamples;
    std::array<double, 0> empty{};
    std::array<double, 1> one{7.0};
    std::array<double, 4> even{40.0, 10.0, 30.0, 20.0};
    std::array<double, 5> odd{5.0, 1.0, 4.0, 2.0, 3.0};
    std::array<double, 2> invalid{0.0, std::numeric_limits<double>::quiet_NaN()};
    const auto single = SummarizeFrameSamples(one);
    const auto a = SummarizeFrameSamples(even);
    const auto b = SummarizeFrameSamples(odd);
    if (SummarizeFrameSamples(empty) || SummarizeFrameSamples(invalid) || !single || !a || !b)
        return false;
    invalid[1] = -1.0;
    if (SummarizeFrameSamples(invalid)) return false;
    invalid[1] = std::numeric_limits<double>::infinity();
    return !SummarizeFrameSamples(invalid) && single->median == 7.0 && single->p99 == 7.0 &&
        a->median == 25.0 && std::abs(a->p95 - 38.5) < 1.0e-10 &&
        std::abs(a->p99 - 39.7) < 1.0e-10 && a->maximum == 40.0 && b->median == 3.0 &&
        std::abs(b->p95 - 4.8) < 1.0e-10 && std::abs(b->p99 - 4.96) < 1.0e-10;
}

bool M3AToneMapProperties()
{
    constexpr std::array<float, 7> inputs{
        0.0F,
        0.01F,
        0.25F,
        1.0F,
        2.0F,
        20.0F,
        std::numeric_limits<float>::max(),
    };

    float previous = 0.0F;
    for (const float input : inputs)
    {
        const float mapped = M3AToneMapScalar(input);
        if (!std::isfinite(mapped) || mapped < 0.0F || mapped > 1.0F || mapped < previous)
        {
            return false;
        }

        previous = mapped;
    }

    const float two = M3AToneMapScalar(2.0F);
    const float twenty = M3AToneMapScalar(20.0F);
    return M3AToneMapScalar(-1.0F) == 0.0F && M3AToneMapScalar(0.0F) == 0.0F &&
           two > 0.0F && two < twenty && twenty < 1.0F;
}

bool M3A1DisplayOutputSelection()
{
    using namespace DeepRun::Render;

    const DisplayOutputCapabilities hdrCapable{
        .output6Available = true,
        .hdrActive = true,
        .scRgbPresentSupported = true,
        .bitsPerColor = 10,
        .minLuminanceNits = 0.05F,
        .maxLuminanceNits = 1'000.0F,
        .maxFullFrameLuminanceNits = 400.0F};
    const DisplayOutputSelection notRequested = SelectDisplayOutputMode(false, hdrCapable);
    const DisplayOutputSelection output6Unavailable = SelectDisplayOutputMode(true, {});
    const DisplayOutputSelection hdrInactive = SelectDisplayOutputMode(
        true,
        {.output6Available = true, .hdrActive = false, .scRgbPresentSupported = true});
    const DisplayOutputSelection scRgbUnsupported = SelectDisplayOutputMode(
        true,
        {.output6Available = true, .hdrActive = true, .scRgbPresentSupported = false});
    const DisplayOutputSelection hdrSelected = SelectDisplayOutputMode(true, hdrCapable);

    return notRequested.mode == DisplayOutputMode::Sdr &&
           notRequested.fallbackReason == DisplayOutputFallbackReason::HdrNotRequested &&
           output6Unavailable.mode == DisplayOutputMode::Sdr &&
           output6Unavailable.fallbackReason == DisplayOutputFallbackReason::Output6Unavailable &&
           hdrInactive.mode == DisplayOutputMode::Sdr &&
           hdrInactive.fallbackReason == DisplayOutputFallbackReason::HdrInactive &&
           scRgbUnsupported.mode == DisplayOutputMode::Sdr &&
           scRgbUnsupported.fallbackReason == DisplayOutputFallbackReason::ScRgbPresentUnsupported &&
           hdrSelected.mode == DisplayOutputMode::HdrScRgb &&
           hdrSelected.fallbackReason == DisplayOutputFallbackReason::None;
}

float M3A1HdrScRgbScalar(const float sceneLinear) noexcept
{
    constexpr float PeakScRgb = 12.5F; // 1,000 nits / 80 nits reference white.
    const float nonNegative = std::max(sceneLinear, 0.0F);
    return PeakScRgb * (1.0F - (PeakScRgb - 1.0F) / (nonNegative + (PeakScRgb - 1.0F)));
}

bool M3A1HdrScRgbMappingProperties()
{
    constexpr std::array<float, 7> inputs{
        0.0F,
        0.25F,
        1.0F,
        2.0F,
        20.0F,
        100.0F,
        std::numeric_limits<float>::max(),
    };

    float previous = 0.0F;
    for (const float input : inputs)
    {
        const float mapped = M3A1HdrScRgbScalar(input);
        if (!std::isfinite(mapped) || mapped < 0.0F || mapped > 12.5F || mapped < previous)
        {
            return false;
        }
        previous = mapped;
    }

    const float referenceWhite = M3A1HdrScRgbScalar(1.0F);
    const float two = M3A1HdrScRgbScalar(2.0F);
    const float twenty = M3A1HdrScRgbScalar(20.0F);
    return M3A1HdrScRgbScalar(-1.0F) == 0.0F && M3A1HdrScRgbScalar(0.0F) == 0.0F &&
           std::abs(referenceWhite - 1.0F) < 0.0001F && two > referenceWhite && twenty > two;
}

bool M3CUnderwaterDepthLightingProperties()
{
    using DeepRun::Render::DepthLightingParameters;
    using DeepRun::Render::EvaluateDepthLighting;
    using DeepRun::Render::ValidateDepthLightingParameters;

    const DepthLightingParameters parameters{
        .surfaceLevelYMeters = 50.0F,
        .attenuationPerMeterRgb = {0.012F, 0.006F, 0.003F},
        .deepAmbientRgb = {0.02F, 0.075F, 0.12F}};
    const auto surface = EvaluateDepthLighting(parameters, 50.0F);
    const auto above = EvaluateDepthLighting(parameters, 80.0F);
    const auto shallow = EvaluateDepthLighting(parameters, -50.0F);
    // The same world Y represents the same WaterBody depth regardless of hypothetical camera position.
    const auto sameDepthDifferentCamera = EvaluateDepthLighting(parameters, -50.0F);
    const auto deep = EvaluateDepthLighting(parameters, -170.0F);
    if (!surface || !above || !shallow || !sameDepthDifferentCamera || !deep || surface->depthMeters != 0.0F ||
        above->depthMeters != 0.0F || shallow->depthMeters != 100.0F || deep->depthMeters != 220.0F)
    {
        return false;
    }
    for (std::size_t channel = 0; channel < 3U; ++channel)
    {
        if (surface->directTransmissionRgb[channel] != 1.0F || above->directTransmissionRgb[channel] != 1.0F ||
            !std::isfinite(deep->directTransmissionRgb[channel]) || deep->directTransmissionRgb[channel] < 0.0F ||
            deep->directTransmissionRgb[channel] > shallow->directTransmissionRgb[channel] ||
            deep->deepAmbientWeightRgb[channel] < 0.0F ||
            0.0F * deep->directTransmissionRgb[channel] + 0.0F * deep->deepAmbientWeightRgb[channel] != 0.0F ||
            shallow->directTransmissionRgb[channel] != sameDepthDifferentCamera->directTransmissionRgb[channel])
        {
            return false;
        }
    }
    if (!(deep->directTransmissionRgb[0] <= deep->directTransmissionRgb[1] &&
          deep->directTransmissionRgb[1] <= deep->directTransmissionRgb[2]) ||
        !(shallow->directTransmissionRgb[0] > deep->directTransmissionRgb[0]))
    {
        return false;
    }

    auto invalid = parameters;
    invalid.attenuationPerMeterRgb[0] = -0.01F;
    auto invalidAmbient = parameters;
    invalidAmbient.deepAmbientRgb[1] = std::numeric_limits<float>::infinity();
    auto extremeSurface = parameters;
    extremeSurface.surfaceLevelYMeters = std::numeric_limits<float>::max();
    return !ValidateDepthLightingParameters(invalid) && !ValidateDepthLightingParameters(invalidAmbient) &&
           !EvaluateDepthLighting(parameters, std::numeric_limits<float>::quiet_NaN()) &&
           !EvaluateDepthLighting(
               extremeSurface,
               -std::numeric_limits<float>::max());
}

bool M3C1ViewPathFogProperties()
{
    using DeepRun::Render::EvaluateDepthLighting;
    using DeepRun::Render::EvaluateViewPathFog;
    using DeepRun::Render::ScenePresentationParameters;
    using DeepRun::Render::ValidateScenePresentationParameters;

    ScenePresentationParameters parameters{
        .depthLighting = {
            .surfaceLevelYMeters = 0.0F,
            .attenuationPerMeterRgb = {0.012F, 0.006F, 0.003F},
            .deepAmbientRgb = {0.02F, 0.075F, 0.12F}},
        .cameraPlaneCenterWorldPosition = {0.0F, -100.0F, 20.0F},
        .cameraViewDirection = {0.0F, 0.0F, -1.0F},
        .fogExtinctionPerMeter = 0.01F,
        .fogColorRgb = {0.00309598F, 0.03954624F, 0.11953843F}};

    // Canonical side-view orthographic rays are parallel to -Z: X displacement cannot lengthen fog path.
    const auto left = EvaluateViewPathFog(parameters, {-250.0F, -100.0F, 0.0F});
    const auto right = EvaluateViewPathFog(parameters, {250.0F, -100.0F, 0.0F});
    // Y displacement similarly does not affect this -Z ray length, but M3-C still observes its world depth.
    const auto shallow = EvaluateViewPathFog(parameters, {0.0F, -100.0F, 0.0F});
    const auto deep = EvaluateViewPathFog(parameters, {0.0F, -200.0F, 0.0F});
    const auto shallowDepth = EvaluateDepthLighting(parameters.depthLighting, -100.0F);
    const auto deepDepth = EvaluateDepthLighting(parameters.depthLighting, -200.0F);
    const auto fartherZ = EvaluateViewPathFog(parameters, {0.0F, -100.0F, -20.0F});
    auto translatedPlane = parameters;
    translatedPlane.cameraPlaneCenterWorldPosition[0] = 250.0F;
    const auto translated = EvaluateViewPathFog(translatedPlane, {0.0F, -100.0F, 0.0F});
    if (!left || !right || !shallow || !deep || !shallowDepth || !deepDepth || !fartherZ || !translated ||
        std::abs(left->submergedPathLengthMeters - 20.0F) > 1.0e-4F ||
        left->submergedPathLengthMeters != right->submergedPathLengthMeters ||
        left->transmission != right->transmission ||
        shallow->submergedPathLengthMeters != deep->submergedPathLengthMeters ||
        shallow->transmission != deep->transmission ||
        shallowDepth->directTransmissionRgb == deepDepth->directTransmissionRgb ||
        !(fartherZ->submergedPathLengthMeters > shallow->submergedPathLengthMeters) ||
        !(fartherZ->transmission < shallow->transmission) ||
        translated->submergedPathLengthMeters != shallow->submergedPathLengthMeters ||
        translated->transmission != shallow->transmission)
    {
        return false;
    }

    // Non-horizontal rays retain analytical plane clipping on reconstructed ray origin -> fragment segments.
    auto clipping = parameters;
    clipping.cameraPlaneCenterWorldPosition = {0.0F, 10.0F, 20.0F};
    clipping.cameraViewDirection = {0.0F, -3.0F, -4.0F}; // normalized internally to (0, -0.6, -0.8)
    const auto dry = EvaluateViewPathFog(clipping, {0.0F, 4.0F, 12.0F});
    clipping.cameraPlaneCenterWorldPosition = {0.0F, -10.0F, 20.0F};
    const auto submerged = EvaluateViewPathFog(clipping, {0.0F, -16.0F, 12.0F});
    clipping.cameraPlaneCenterWorldPosition = {0.0F, 5.0F, 20.0F};
    const auto crossing = EvaluateViewPathFog(clipping, {0.0F, -1.0F, 12.0F});
    if (!dry || !submerged || !crossing || dry->submergedPathLengthMeters != 0.0F ||
        std::abs(submerged->submergedPathLengthMeters - 10.0F) > 1.0e-4F ||
        std::abs(crossing->submergedPathLengthMeters - (10.0F / 6.0F)) > 1.0e-4F ||
        !std::isfinite(crossing->transmission) || crossing->transmission <= 0.0F ||
        crossing->transmission >= 1.0F)
    {
        return false;
    }

    auto invalidExtinction = parameters;
    invalidExtinction.fogExtinctionPerMeter = -0.01F;
    auto invalidPlaneCenter = parameters;
    invalidPlaneCenter.cameraPlaneCenterWorldPosition[2] = std::numeric_limits<float>::infinity();
    auto zeroDirection = parameters;
    zeroDirection.cameraViewDirection = {};
    auto nonFiniteDirection = parameters;
    nonFiniteDirection.cameraViewDirection[1] = std::numeric_limits<float>::quiet_NaN();
    auto invalidColor = parameters;
    invalidColor.fogColorRgb[1] = std::numeric_limits<float>::quiet_NaN();
    return !ValidateScenePresentationParameters(invalidExtinction) &&
           !ValidateScenePresentationParameters(invalidPlaneCenter) &&
           !ValidateScenePresentationParameters(zeroDirection) &&
           !ValidateScenePresentationParameters(nonFiniteDirection) &&
           !ValidateScenePresentationParameters(invalidColor) &&
           !EvaluateViewPathFog(parameters, {0.0F, std::numeric_limits<float>::infinity(), 0.0F});
}

bool M3DUnderwaterParticleFieldProperties()
{
    using DeepRun::Render::EvaluateSuspendedParticlePosition;
    using DeepRun::Render::GenerateSuspendedParticleField;
    using DeepRun::Render::SuspendedParticleFieldParameters;
    using DeepRun::Render::SuspendedParticleVerticalWrapPeriodSeconds;
    using DeepRun::Render::ValidateSuspendedParticleFieldParameters;

    const SuspendedParticleFieldParameters parameters{
        .seed = 0x4D334430U,
        .particleCount = 256U,
        .minimumWorldPosition = {-320.0F, -260.0F, -20.0F},
        .maximumWorldPosition = {320.0F, -15.0F, 20.0F},
        .particleSizeMeters = 1.15F,
        .particleOpacity = 0.18F,
        .verticalDriftMetersPerSecond = 0.16F,
        .lateralOscillationAmplitudeMeters = 1.4F,
        .lateralOscillationAngularFrequency = 0.23F};
    const auto first = GenerateSuspendedParticleField(parameters);
    const auto repeated = GenerateSuspendedParticleField(parameters);
    auto differentSeed = parameters;
    differentSeed.seed ^= 0x9E3779B9U;
    const auto different = GenerateSuspendedParticleField(differentSeed);
    const auto wrapPeriod = SuspendedParticleVerticalWrapPeriodSeconds(parameters);
    if (!first || !repeated || !different || !wrapPeriod || first->size() != parameters.particleCount ||
        repeated->size() != parameters.particleCount || different->size() != parameters.particleCount)
    {
        return false;
    }

    bool differentLayout = false;
    for (std::size_t index = 0; index < first->size(); ++index)
    {
        const auto& particle = first->at(index);
        const auto& repeatedParticle = repeated->at(index);
        const auto& differentParticle = different->at(index);
        if (particle.initialWorldPosition != repeatedParticle.initialWorldPosition ||
            particle.phaseRadians != repeatedParticle.phaseRadians ||
            particle.initialWorldPosition[0] < parameters.minimumWorldPosition[0] ||
            particle.initialWorldPosition[0] > parameters.maximumWorldPosition[0] ||
            particle.initialWorldPosition[1] < parameters.minimumWorldPosition[1] ||
            particle.initialWorldPosition[1] > parameters.maximumWorldPosition[1] ||
            particle.initialWorldPosition[2] < parameters.minimumWorldPosition[2] ||
            particle.initialWorldPosition[2] > parameters.maximumWorldPosition[2] ||
            particle.initialWorldPosition[1] >= 0.0F ||
            !std::isfinite(particle.initialWorldPosition[0]) || !std::isfinite(particle.initialWorldPosition[1]) ||
            !std::isfinite(particle.initialWorldPosition[2]) || !std::isfinite(particle.phaseRadians))
        {
            return false;
        }
        differentLayout = differentLayout || particle.initialWorldPosition != differentParticle.initialWorldPosition ||
                          particle.phaseRadians != differentParticle.phaseRadians;

        const auto atTime = EvaluateSuspendedParticlePosition(parameters, particle, 7.25F);
        const auto repeatedTime = EvaluateSuspendedParticlePosition(parameters, particle, 7.25F);
        const auto afterWrap = EvaluateSuspendedParticlePosition(parameters, particle, 7.25F + *wrapPeriod);
        if (!atTime || !repeatedTime || !afterWrap || *atTime != *repeatedTime ||
            std::abs((*atTime)[1] - (*afterWrap)[1]) > 1.0e-3F)
        {
            return false;
        }
        for (const auto& position : {*atTime, *afterWrap})
        {
            for (std::size_t axis = 0; axis < position.size(); ++axis)
            {
                if (!std::isfinite(position[axis]) || position[axis] < parameters.minimumWorldPosition[axis] ||
                    position[axis] > parameters.maximumWorldPosition[axis])
                {
                    return false;
                }
            }
        }
    }

    auto negativeSize = parameters;
    negativeSize.particleSizeMeters = -0.1F;
    auto invalidOpacity = parameters;
    invalidOpacity.particleOpacity = std::numeric_limits<float>::infinity();
    auto invalidBounds = parameters;
    invalidBounds.minimumWorldPosition[2] = std::numeric_limits<float>::quiet_NaN();
    auto tooManyParticles = parameters;
    tooManyParticles.particleCount = 513U;
    return differentLayout && !ValidateSuspendedParticleFieldParameters(negativeSize) &&
           !ValidateSuspendedParticleFieldParameters(invalidOpacity) &&
           !ValidateSuspendedParticleFieldParameters(invalidBounds) &&
           !ValidateSuspendedParticleFieldParameters(tooManyParticles) &&
           !EvaluateSuspendedParticlePosition(parameters, first->front(), -1.0F);
}

bool M3E1WaveDefinitionValidation()
{
    using namespace DeepRun::Marine;
    WaterBodyConfig config{.surfaceLevelY = 0.0F, .densityKgPerCubicMeter = 1025.0F,
                           .waves = M3WaterWaveField};
    if (!WaterBody::Create(config)) return false;
    // Each scalar rejects both NaN and infinity; invalid finite domain values are also rejected.
    const float invalid[] = {std::numeric_limits<float>::quiet_NaN(),
                            std::numeric_limits<float>::infinity()};
    for (const float value : invalid)
    {
        for (int field = 0; field < 5; ++field)
        {
            auto changed = config;
            auto& wave = changed.waves->components[0];
            float* fields[] = {&wave.amplitudeMeters, &wave.wavelengthMeters,
                              &wave.angularFrequencyRadiansPerSecond, &wave.phaseOffsetRadians,
                              &wave.horizontalSteepness};
            *fields[field] = value;
            if (WaterBody::Create(changed)) return false;
        }
    }
    for (const float value : {0.0F, -1.0F})
    {
        for (int field = 0; field < 3; ++field)
        {
            auto changed = config;
            auto& wave = changed.waves->components[0];
            float* fields[] = {&wave.amplitudeMeters, &wave.wavelengthMeters,
                              &wave.angularFrequencyRadiansPerSecond};
            *fields[field] = value;
            if (WaterBody::Create(changed)) return false;
        }
    }
    for (const float q : {-0.1F, 1.1F})
    {
        auto changed = config;
        changed.waves->components[0].horizontalSteepness = q;
        if (WaterBody::Create(changed)) return false;
    }
    auto tooLarge = config;
    tooLarge.waves->components[0].amplitudeMeters = 3.1F;
    auto combined = config;
    for (auto& wave : combined.waves->components) wave.amplitudeMeters = 1.5F;
    auto folding = config;
    folding.waves->components[0].wavelengthMeters = 1.0F;
    return !WaterBody::Create(tooLarge) && !WaterBody::Create(combined) && !WaterBody::Create(folding);
}

bool M3E1WaveQueryAndAdapterParity()
{
    using namespace DeepRun::Marine;
    const auto water = WaterBody::Create({.surfaceLevelY = 7.0F, .densityKgPerCubicMeter = 1025.0F,
                                         .waves = M3WaterWaveField});
    const auto flat = WaterBody::Create({.surfaceLevelY = 7.0F, .densityKgPerCubicMeter = 1025.0F});
    if (!water || !flat) return false;
    const auto render = DeepRun::Game::BuildGerstnerSurfacePresentation(*water);
    if (!DeepRun::Render::ValidateGerstnerSurfacePresentationParameters(render) ||
        render.referenceLevelY != water->Config().surfaceLevelY) return false;
    float amplitude = 0.0F;
    for (std::size_t i = 0; i < render.components.size(); ++i)
    {
        const auto& a = water->Config().waves->components[i];
        const auto& b = render.components[i];
        if (a.amplitudeMeters != b.amplitudeMeters || a.wavelengthMeters != b.wavelengthMeters ||
            a.angularFrequencyRadiansPerSecond != b.angularFrequencyRadiansPerSecond ||
            a.phaseOffsetRadians != b.phaseOffsetRadians || a.horizontalSteepness != b.horizontalSteepness)
            return false;
        amplitude += a.amplitudeMeters;
    }
    if (std::abs(amplitude - 2.90F) > 1.0e-6F) return false;
    bool distinguishesNaiveQuery = false;
    // Covers the whole bounded mesh, including neighborhoods of extrema, at several phase times.
    // 1e-4 m allows published float X rounding and Render's float 2*pi versus Marine double phase math.
    for (const float time : {0.0F, 1.0F, 6.0F, 60.0F})
    {
        for (int step = -680; step <= 680; ++step)
        {
            const float u = static_cast<float>(step) * 0.5F;
            const auto visual = DeepRun::Render::EvaluateGerstnerSurfacePresentation(render, u, time);
            if (!visual) return false;
            const DeepRun::Physics::PhysicsVector3 position{visual->x, 3.0F, 9.0F};
            const auto sample = water->SampleWaveSurface(position, time);
            const auto repeated = water->SampleWaveSurface(position, time);
            const auto naive = DeepRun::Render::EvaluateGerstnerSurfacePresentation(render, visual->x, time);
            if (!sample || !repeated || !naive || !sample->surfaceNormal.IsFinite()) return false;
            const auto n = sample->surfaceNormal;
            if (std::abs(sample->surfaceLevelY - visual->y) > 1.0e-4F ||
                sample->surfaceLevelY != repeated->surfaceLevelY || n != repeated->surfaceNormal ||
                sample->signedDepthMeters != repeated->signedDepthMeters ||
                std::abs(sample->surfaceLevelY - 7.0F) > 2.90001F ||
                std::abs(sample->signedDepthMeters - (sample->surfaceLevelY - position.y)) > 1.0e-6F ||
                std::abs(n.x * n.x + n.y * n.y + n.z * n.z - 1.0F) > 1.0e-6F || n.y <= 0.0F || n.z != 0.0F)
                return false;
            distinguishesNaiveQuery |= std::abs(sample->surfaceLevelY - naive->y) > 0.01F;
            if (step % 40 == 0)
            {
                const auto left = DeepRun::Render::EvaluateGerstnerSurfacePresentation(render, u - 0.05F, time);
                const auto right = DeepRun::Render::EvaluateGerstnerSurfacePresentation(render, u + 0.05F, time);
                if (!left || !right) return false;
                const float dx = right->x - left->x;
                const float dy = right->y - left->y;
                const float length = std::hypot(dx, dy);
                if (std::abs(n.x + dy / length) > 0.001F || std::abs(n.y - dx / length) > 0.001F)
                    return false;
            }
        }
    }
    for (const float y : {-20.0F, 7.0F, 20.0F})
    {
        const DeepRun::Physics::PhysicsVector3 position{37.5F, y, -12.0F};
        const auto a = flat->Sample(position);
        const auto b = flat->SampleWaveSurface(position, 6.0);
        const auto c = water->Sample(position);
        if (!a || !b || !c || a->surfaceLevelY != b->surfaceLevelY || a->surfaceLevelY != c->surfaceLevelY ||
            a->surfaceNormal != b->surfaceNormal || a->surfaceNormal != c->surfaceNormal ||
            a->signedDepthMeters != b->signedDepthMeters || a->signedDepthMeters != c->signedDepthMeters)
            return false;
    }
    const auto buoyancy = E3NeutralBuoyancy(2000.0F, 1025.0F);
    const BuoyancyPose pose{.worldPositionMeters = {37.5F, 7.0F, 0.0F}};
    const auto flatForce = BuoyancySystem::Calculate(*flat, buoyancy, pose, 9.81F);
    const auto waveForce = BuoyancySystem::Calculate(*water, buoyancy, pose, 9.81F);
    if (!flatForce || !waveForce || flatForce->totalForceNewtons != waveForce->totalForceNewtons ||
        flatForce->totalSubmergedVolumeCubicMeters != waveForce->totalSubmergedVolumeCubicMeters) return false;
    for (std::size_t i = 0; i < flatForce->points.size(); ++i)
        if (flatForce->points[i].signedDepthMeters != waveForce->points[i].signedDepthMeters ||
            flatForce->points[i].forceNewtons != waveForce->points[i].forceNewtons) return false;
    const auto first = water->SampleWaveSurface({37.5F, 0.0F, 0.0F}, 0.0);
    const auto later = water->SampleWaveSurface({37.5F, 0.0F, 0.0F}, 6.0);
    // Aligned test phases give known crest/trough at u=0 (and world X approximately zero).
    for (const float sign : {-1.0F, 1.0F})
    {
        auto extremeConfig = water->Config();
        for (auto& component : extremeConfig.waves->components)
            component.phaseOffsetRadians = sign * 1.57079632679F;
        const auto extreme = WaterBody::Create(extremeConfig);
        if (!extreme) return false;
        for (const float x : {-0.001F, 0.0F, 0.001F})
        {
            const auto sample = extreme->SampleWaveSurface({x, 0.0F, 0.0F}, 0.0);
            if (!sample || std::abs(sample->surfaceLevelY - (7.0F + sign * amplitude)) > 1.0e-5F ||
                !sample->surfaceNormal.IsFinite() || sample->surfaceNormal.y < 0.999F) return false;
        }
    }
    auto overflowConfig = water->Config();
    overflowConfig.surfaceLevelY = (std::numeric_limits<float>::max)();
    const auto overflow = WaterBody::Create(overflowConfig);
    if (!overflow || overflow->SampleWaveSurface({0.0F, -(std::numeric_limits<float>::max)(), 0.0F}, 0.0))
        return false;
    return distinguishesNaiveQuery && first && later && first->surfaceLevelY != later->surfaceLevelY &&
           !water->SampleWaveSurface({}, -1.0) &&
           !water->SampleWaveSurface({}, std::numeric_limits<double>::infinity()) &&
           !water->SampleWaveSurface({}, std::numeric_limits<double>::quiet_NaN()) &&
           !water->SampleWaveSurface({std::numeric_limits<float>::infinity(), 0.0F, 0.0F}, 0.0);
}

DeepRun::Marine::BuoyancyComponent M3FSurfaceFloatBuoyancy(const float densityKgPerCubicMeter)
{
    constexpr float MassKg = 1000.0F;
    const float pointPotentialVolume = MassKg / densityKgPerCubicMeter;
    return {.points = {
        {.bodyLocalPositionMeters = {1.0F, 0.0F, 0.0F},
         .displacedVolumeCubicMeters = pointPotentialVolume,
         .submersionHalfHeightMeters = 0.55F},
        {.bodyLocalPositionMeters = {-1.0F, 0.0F, 0.0F},
         .displacedVolumeCubicMeters = pointPotentialVolume,
         .submersionHalfHeightMeters = 0.55F}}};
}

bool SameBuoyancyResult(const DeepRun::Marine::BuoyancyResult& first,
                        const DeepRun::Marine::BuoyancyResult& second)
{
    if (first.totalForceNewtons != second.totalForceNewtons ||
        first.totalSubmergedVolumeCubicMeters != second.totalSubmergedVolumeCubicMeters ||
        first.points.size() != second.points.size())
    {
        return false;
    }
    for (std::size_t index = 0; index < first.points.size(); ++index)
    {
        const auto& a = first.points[index];
        const auto& b = second.points[index];
        if (a.worldPositionMeters != b.worldPositionMeters || a.signedDepthMeters != b.signedDepthMeters ||
            a.submergedFraction != b.submergedFraction ||
            a.submergedVolumeCubicMeters != b.submergedVolumeCubicMeters || a.forceNewtons != b.forceNewtons)
        {
            return false;
        }
    }
    return true;
}

bool M3FSurfaceFloatModelContract()
{
    const DeepRun::Assets::ModelAsset model = DeepRun::Game::BuildM3SurfaceFloatModel();
    const auto layout = DeepRun::Render::BuildIndexedGeometryLayout(model);
    const auto draws = DeepRun::Render::PrepareModelDraws(model);
    if (!layout || !draws || model.materials.size() != 1U || model.primitives.size() != 1U ||
        model.nodes.size() != 1U || draws->size() != 1U || layout->totals.primitiveCount != 1U ||
        layout->totals.vertexCount != 24U || layout->totals.indexCount != 36U ||
        model.bounds.minimum.x != -3.0F || model.bounds.maximum.x != 3.0F ||
        model.bounds.minimum.y != -0.5F || model.bounds.maximum.y != 3.0F ||
        model.bounds.minimum.z != -0.5F || model.bounds.maximum.z != 0.5F)
    {
        return false;
    }
    const auto& primitive = model.primitives.front();
    if (!primitive.hasNormals || primitive.materialIndex != 0U || primitive.indices.size() != 36U)
    {
        return false;
    }
    for (const auto& vertex : primitive.vertices)
    {
        const float normalLengthSquared = vertex.normal.x * vertex.normal.x + vertex.normal.y * vertex.normal.y +
                                          vertex.normal.z * vertex.normal.z;
        if (!DeepRun::Game::IsFinite(vertex.position) || !DeepRun::Game::IsFinite(vertex.normal) ||
            std::abs(normalLengthSquared - 1.0F) > 1.0e-6F)
        {
            return false;
        }
    }
    return model.nodes.front().primitiveIndices == std::vector<std::size_t>{0U} &&
           model.materials.front().baseColorFactor[3] == 1.0F;
}

bool M3FWaveBuoyancyIsExplicitAndLocal()
{
    using namespace DeepRun::Marine;
    constexpr float Density = 1025.0F;
    constexpr float Gravity = 9.81F;
    const auto flatWater = WaterBody::Create({.surfaceLevelY = 0.0F, .densityKgPerCubicMeter = Density});
    const auto waveWater = WaterBody::Create(
        {.surfaceLevelY = 0.0F, .densityKgPerCubicMeter = Density, .waves = M3WaterWaveField});
    const BuoyancyComponent component = M3FSurfaceFloatBuoyancy(Density);
    const BuoyancyPose pose{.worldPositionMeters = {140.0F, 0.0F, 0.0F}};
    if (!flatWater || !waveWater) return false;

    const auto flat = BuoyancySystem::Calculate(*flatWater, component, pose, Gravity);
    const auto preservedFlat = BuoyancySystem::Calculate(*waveWater, component, pose, Gravity);
    BuoyancyResult disabledWave;
    disabledWave.points.reserve(component.points.size());
    const auto disabledCalculated = BuoyancySystem::CalculateWaveSurface(
        *flatWater, component, pose, Gravity, 3.0, disabledWave);
    if (!flat || !preservedFlat || !disabledCalculated || !SameBuoyancyResult(*flat, *preservedFlat) ||
        !SameBuoyancyResult(*flat, disabledWave))
    {
        return false;
    }

    BuoyancyResult first;
    BuoyancyResult repeated;
    BuoyancyResult later;
    first.points.reserve(component.points.size());
    repeated.points.reserve(component.points.size());
    later.points.reserve(component.points.size());
    if (!BuoyancySystem::CalculateWaveSurface(*waveWater, component, pose, Gravity, 2.0, first) ||
        !BuoyancySystem::CalculateWaveSurface(*waveWater, component, pose, Gravity, 2.0, repeated) ||
        !BuoyancySystem::CalculateWaveSurface(*waveWater, component, pose, Gravity, 6.0, later) ||
        !SameBuoyancyResult(first, repeated))
    {
        return false;
    }

    const float potentialVolume = 2.0F * (1000.0F / Density);
    bool differentTime = first.totalForceNewtons != later.totalForceNewtons;
    bool foreAftDifferent = first.points[0].signedDepthMeters != first.points[1].signedDepthMeters ||
                             first.points[0].forceNewtons != first.points[1].forceNewtons;
    for (const BuoyancyPointResult& point : first.points)
    {
        const auto surface = waveWater->SampleWaveSurface(point.worldPositionMeters, 2.0);
        const float forceMagnitude = std::sqrt(point.forceNewtons.x * point.forceNewtons.x +
                                               point.forceNewtons.y * point.forceNewtons.y +
                                               point.forceNewtons.z * point.forceNewtons.z);
        if (!surface || point.signedDepthMeters != surface->signedDepthMeters || !point.worldPositionMeters.IsFinite() ||
            !point.forceNewtons.IsFinite() || point.submergedFraction < 0.0F || point.submergedFraction > 1.0F ||
            point.submergedVolumeCubicMeters < 0.0F || surface->surfaceNormal.y <= 0.0F ||
            (forceMagnitude > 0.0F &&
             (std::abs(point.forceNewtons.x / forceMagnitude - surface->surfaceNormal.x) > 1.0e-5F ||
              std::abs(point.forceNewtons.y / forceMagnitude - surface->surfaceNormal.y) > 1.0e-5F ||
              std::abs(point.forceNewtons.z / forceMagnitude - surface->surfaceNormal.z) > 1.0e-5F)))
        {
            return false;
        }
    }
    BuoyancyResult invalidTime;
    const auto invalid = BuoyancySystem::CalculateWaveSurface(
        *waveWater, component, pose, Gravity, -1.0, invalidTime);
    const auto nonFinite = BuoyancySystem::CalculateWaveSurface(
        *waveWater, component, pose, Gravity, std::numeric_limits<double>::infinity(), invalidTime);
    return differentTime && foreAftDifferent && first.totalSubmergedVolumeCubicMeters >= 0.0F &&
           first.totalSubmergedVolumeCubicMeters <= potentialVolume && !invalid && !nonFinite &&
           invalid.error().code == BuoyancyErrorCode::InvalidSimulationTime &&
           nonFinite.error().code == BuoyancyErrorCode::InvalidSimulationTime;
}

struct M3FFloatMotion final
{
    float minimumY = std::numeric_limits<float>::infinity();
    float maximumY = -std::numeric_limits<float>::infinity();
    float minimumPitchRadians = std::numeric_limits<float>::infinity();
    float maximumPitchRadians = -std::numeric_limits<float>::infinity();
    PhysicsBodyState finalState{};
};

std::optional<M3FFloatMotion> SimulateM3FSurfaceFloat(const bool wavesEnabled)
{
    constexpr float Density = 1025.0F;
    constexpr float Mass = 1000.0F;
    constexpr int Steps = 1200; // 20 s at the canonical 60 Hz fixed tick.
    DeepRun::Diagnostics::Logger logger;
    DeepRun::Physics::PhysicsWorld world(logger);
    const auto water = WaterBody::Create(
        {.surfaceLevelY = 0.0F,
         .densityKgPerCubicMeter = Density,
         .waves = wavesEnabled
                      ? std::optional<DeepRun::Marine::WaterWaveFieldDefinition>{DeepRun::Marine::M3WaterWaveField}
                      : std::nullopt});
    if (!world.Initialize() || !water) return std::nullopt;
    const auto initialSurface = water->SampleWaveSurface({140.0F, 0.0F, 0.0F}, 0.0);
    if (!initialSurface) return std::nullopt;
    DeepRun::Physics::PhysicsDegreesOfFreedom dof;
    dof.translationX = true;
    dof.translationY = true;
    dof.translationZ = false;
    dof.rotationX = false;
    dof.rotationY = false;
    dof.rotationZ = true;
    const auto handle = world.CreateDynamicBoxBody({
        .halfExtents = {1.5F, 0.5F, 0.5F},
        .mass = Mass,
        .position = {140.0F, initialSurface->surfaceLevelY, 0.0F},
        .orientation = {},
        .gravityEnabled = true,
        .linearDamping = 0.9F,
        .angularDamping = 2.0F,
        .initialLinearVelocity = {},
        .initialAngularVelocity = {},
        .degreesOfFreedom = dof});
    const auto gravity = world.Gravity();
    if (!handle.IsValid() || !gravity || !gravity->IsFinite()) return std::nullopt;
    const float gravityMagnitude = std::sqrt(
        gravity->x * gravity->x + gravity->y * gravity->y + gravity->z * gravity->z);
    const BuoyancyComponent buoyancy = M3FSurfaceFloatBuoyancy(Density);
    BuoyancyResult result;
    result.points.reserve(buoyancy.points.size());
    M3FFloatMotion motion;
    for (int step = 0; step < Steps; ++step)
    {
        const auto before = world.GetBodyState(handle);
        if (!before || !StateIsFinite(*before) ||
            !BuoyancySystem::CalculateWaveSurface(
                *water,
                buoyancy,
                {.worldPositionMeters = before->position, .worldOrientation = before->orientation},
                gravityMagnitude,
                static_cast<double>(step) * E3FixedDeltaSeconds,
                result))
        {
            return std::nullopt;
        }
        for (const DeepRun::Marine::BuoyancyPointResult& point : result.points)
            if (!world.AddForceAtWorldPosition(handle, point.forceNewtons, point.worldPositionMeters)) return std::nullopt;
        world.Step(E3FixedDeltaSeconds);
        const auto after = world.GetBodyState(handle);
        if (!after || !StateIsFinite(*after)) return std::nullopt;
        const float pitch = 2.0F * std::atan2(after->orientation.z, after->orientation.w);
        motion.minimumY = (std::min)(motion.minimumY, after->position.y);
        motion.maximumY = (std::max)(motion.maximumY, after->position.y);
        motion.minimumPitchRadians = (std::min)(motion.minimumPitchRadians, pitch);
        motion.maximumPitchRadians = (std::max)(motion.maximumPitchRadians, pitch);
        motion.finalState = *after;
    }
    return motion;
}

bool M3FSurfaceFloatWavePhysicsIntegration()
{
    const auto wave = SimulateM3FSurfaceFloat(true);
    const auto flat = SimulateM3FSurfaceFloat(false);
    if (!wave || !flat) return false;
    const float waveHeave = wave->maximumY - wave->minimumY;
    const float wavePitch = wave->maximumPitchRadians - wave->minimumPitchRadians;
    const float flatHeave = flat->maximumY - flat->minimumY;
    std::cout << "[M3-F evidence] wave Y [" << wave->minimumY << ", " << wave->maximumY << "] pitch ["
              << wave->minimumPitchRadians << ", " << wave->maximumPitchRadians << "]; flat Y ["
              << flat->minimumY << ", " << flat->maximumY << "] pitch [" << flat->minimumPitchRadians << ", "
              << flat->maximumPitchRadians << "]\n";
    // A 20-second, 1/60 s deterministic envelope: float remains near the +/-2.90 m free-surface band,
    // responds materially differently from the flat control, and never exhibits runaway heave or pitch.
    return StateIsFinite(wave->finalState) && StateIsFinite(flat->finalState) && wave->minimumY > -5.0F &&
           wave->maximumY < 5.0F && std::abs(wave->minimumPitchRadians) < 0.75F &&
           std::abs(wave->maximumPitchRadians) < 0.75F && waveHeave > 0.15F && wavePitch > 0.002F &&
           (std::abs(wave->finalState.position.y - flat->finalState.position.y) > 0.05F ||
            std::abs(waveHeave - flatHeave) > 0.05F);
}

bool M3EGerstnerSurfacePresentationProperties()
{
    using DeepRun::Render::EvaluateGerstnerSurfacePresentation;
    using DeepRun::Render::GenerateGerstnerSurfaceBaseMesh;
    using DeepRun::Render::GerstnerSurfacePresentationParameters;
    using DeepRun::Render::MaximumGerstnerCombinedVerticalAmplitudeMeters;
    using DeepRun::Render::ValidateGerstnerSurfacePresentationParameters;

    const GerstnerSurfacePresentationParameters parameters{
        .minimumX = -340.0F,
        .maximumX = 340.0F,
        .referenceLevelY = 0.0F,
        .bottomFillY = -600.0F,
        .horizontalSampleCount = 257U,
        .components = {{
            {.amplitudeMeters = 1.75F,
             .wavelengthMeters = 100.0F,
             .angularFrequencyRadiansPerSecond = 0.28F,
             .phaseOffsetRadians = 0.20F,
             .horizontalSteepness = 0.55F},
            {.amplitudeMeters = 0.80F,
             .wavelengthMeters = 45.0F,
             .angularFrequencyRadiansPerSecond = 0.48F,
             .phaseOffsetRadians = 1.40F,
             .horizontalSteepness = 0.40F},
            {.amplitudeMeters = 0.35F,
             .wavelengthMeters = 20.0F,
             .angularFrequencyRadiansPerSecond = 0.82F,
             .phaseOffsetRadians = 2.30F,
             .horizontalSteepness = 0.20F}}},
        .deepFillRgb = {0.00309598F, 0.03954624F, 0.11953843F},
        .surfaceTintRgb = {0.0065F, 0.075F, 0.18F}};
    const auto first = GenerateGerstnerSurfaceBaseMesh(parameters);
    const auto repeated = GenerateGerstnerSurfaceBaseMesh(parameters);
    const auto maximumAmplitude = MaximumGerstnerCombinedVerticalAmplitudeMeters(parameters);
    if (!ValidateGerstnerSurfacePresentationParameters(parameters) || !first || !repeated || !maximumAmplitude ||
        std::abs(*maximumAmplitude - 2.90F) > 1.0e-5F || first->vertices.size() != 514U ||
        first->indices.size() != 1536U || repeated->vertices.size() != first->vertices.size() ||
        repeated->indices.size() != first->indices.size() || parameters.minimumX > -300.0F ||
        parameters.maximumX < 300.0F || parameters.bottomFillY >= -550.0F)
    {
        return false;
    }

    for (std::size_t index = 0; index < first->vertices.size(); ++index)
    {
        const auto& vertex = first->vertices[index];
        const auto& repeatedVertex = repeated->vertices[index];
        if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y) || !std::isfinite(vertex.surfaceWeight) ||
            vertex.x != repeatedVertex.x || vertex.y != repeatedVertex.y ||
            vertex.surfaceWeight != repeatedVertex.surfaceWeight ||
            (index % 2U == 0U &&
             (vertex.y != parameters.referenceLevelY || vertex.surfaceWeight != 1.0F)) ||
            (index % 2U == 1U && (vertex.y != parameters.bottomFillY || vertex.surfaceWeight != 0.0F)))
        {
            return false;
        }
    }
    if (first->vertices.front().x != parameters.minimumX || first->vertices[first->vertices.size() - 2U].x != parameters.maximumX)
    {
        return false;
    }
    for (std::size_t index = 0; index < first->indices.size(); index += 3U)
    {
        const std::uint32_t a = first->indices[index];
        const std::uint32_t b = first->indices[index + 1U];
        const std::uint32_t c = first->indices[index + 2U];
        if (a >= first->vertices.size() || b >= first->vertices.size() || c >= first->vertices.size() ||
            a == b || b == c || a == c || first->indices[index] != repeated->indices[index] ||
            first->indices[index + 1U] != repeated->indices[index + 1U] ||
            first->indices[index + 2U] != repeated->indices[index + 2U])
        {
            return false;
        }
    }

    const auto atZero = EvaluateGerstnerSurfacePresentation(parameters, 37.5F, 0.0F);
    const auto atZeroRepeated = EvaluateGerstnerSurfacePresentation(parameters, 37.5F, 0.0F);
    const auto later = EvaluateGerstnerSurfacePresentation(parameters, 37.5F, 6.0F);
    if (!atZero || !atZeroRepeated || !later || *atZero != *atZeroRepeated ||
        (!std::isfinite(atZero->x) || !std::isfinite(atZero->y)) ||
        (std::abs(later->x - atZero->x) <= 1.0e-5F && std::abs(later->y - atZero->y) <= 1.0e-5F))
    {
        return false;
    }

    auto zeroWavelength = parameters;
    zeroWavelength.components[0].wavelengthMeters = 0.0F;
    auto negativeWavelength = parameters;
    negativeWavelength.components[1].wavelengthMeters = -1.0F;
    auto nonFiniteAmplitude = parameters;
    nonFiniteAmplitude.components[2].amplitudeMeters = std::numeric_limits<float>::infinity();
    auto tooFewSamples = parameters;
    tooFewSamples.horizontalSampleCount = 1U;
    auto tooManySamples = parameters;
    tooManySamples.horizontalSampleCount = 514U;
    auto foldingProfile = parameters;
    foldingProfile.components[0].horizontalSteepness = 1.0F;
    foldingProfile.components[0].wavelengthMeters = 5.0F;
    auto shallowBottom = parameters;
    shallowBottom.bottomFillY = -2.0F;
    return !ValidateGerstnerSurfacePresentationParameters(zeroWavelength) &&
           !ValidateGerstnerSurfacePresentationParameters(negativeWavelength) &&
           !ValidateGerstnerSurfacePresentationParameters(nonFiniteAmplitude) &&
           !ValidateGerstnerSurfacePresentationParameters(tooFewSamples) &&
           !ValidateGerstnerSurfacePresentationParameters(tooManySamples) &&
           !ValidateGerstnerSurfacePresentationParameters(foldingProfile) &&
           !ValidateGerstnerSurfacePresentationParameters(shallowBottom) &&
           !EvaluateGerstnerSurfacePresentation(parameters, 0.0F, -0.1F) &&
           !EvaluateGerstnerSurfacePresentation(parameters, std::numeric_limits<float>::infinity(), 0.0F);
}

bool M3B1StaticBodyContract()
{
    using namespace DeepRun::Physics;
    DeepRun::Diagnostics::Logger logger;
    PhysicsWorld world(logger);
    PhysicsError error;
    const StaticBoxBodyCreateInfo floor{{10, 1, 3}, {0, -1, 0}};
    if (world.CreateStaticBoxBody(floor, &error).IsValid() ||
        error.code != PhysicsErrorCode::NotInitialized || !world.Initialize()) return false;
    for (const float invalid : {0.0F, -1.0F, std::numeric_limits<float>::infinity(),
                               std::numeric_limits<float>::quiet_NaN()})
    {
        auto bad = floor;
        bad.halfExtents.x = invalid;
        if (world.CreateStaticBoxBody(bad, &error).IsValid() || error.code != PhysicsErrorCode::InvalidInput)
            return false;
    }
    auto bad = floor;
    bad.position.y = std::numeric_limits<float>::quiet_NaN();
    if (world.CreateStaticBoxBody(bad).IsValid()) return false;
    const auto fixed = world.CreateStaticBoxBody(floor);
    const auto falling = world.CreateDynamicBoxBody({.halfExtents = {1,1,1}, .mass = 10, .position = {0,5,0}});
    if (!fixed.IsValid() || !falling.IsValid()) return false;
    for (int i = 0; i < 600; ++i) world.Step(1.0F / 60.0F);
    const auto fixedState = world.GetBodyState(fixed);
    const auto resting = world.GetBodyState(falling);
    if (!fixedState || fixedState->position != floor.position || fixedState->linearVelocity != PhysicsVector3{} ||
        !resting || std::abs(resting->position.y - 1) > 0.1F || std::abs(resting->linearVelocity.y) > 0.1F)
        return false;
    return world.DestroyBody(fixed) && !world.GetBodyState(fixed) && !world.DestroyBody(fixed) &&
           world.GetBodyState(falling).has_value();
}

bool M3B1StaticBodyRejectsMutation()
{
    using namespace DeepRun::Physics;
    DeepRun::Diagnostics::Logger logger;
    PhysicsWorld world(logger);
    if (!world.Initialize())
    {
        return false;
    }

    const StaticBoxBodyCreateInfo staticInfo{{10.0F, 1.0F, 3.0F}, {3.0F, -2.0F, 0.0F}};
    const auto staticBody = world.CreateStaticBoxBody(staticInfo);
    DynamicBoxBodyCreateInfo dynamicInfo;
    dynamicInfo.halfExtents = {1.0F, 1.0F, 1.0F};
    dynamicInfo.mass = 10.0F;
    dynamicInfo.position = {-3.0F, 5.0F, 0.0F};
    dynamicInfo.gravityEnabled = false;
    const auto dynamicBody = world.CreateDynamicBoxBody(dynamicInfo);
    const auto before = world.GetBodyState(staticBody);
    if (!staticBody.IsValid() || !dynamicBody.IsValid() || !before)
    {
        return false;
    }

    PhysicsError error;
    if (world.AddForceAtWorldPosition(staticBody, {50.0F, 0.0F, 0.0F}, before->position, &error) ||
        error.code != PhysicsErrorCode::InvalidInput || error.message != "force cannot be applied to a static body" ||
        world.AddTorque(staticBody, {0.0F, 0.0F, 50.0F}, &error) ||
        error.code != PhysicsErrorCode::InvalidInput || error.message != "torque cannot be applied to a static body" ||
        !world.GetBodyState(staticBody) ||
        !world.AddForceAtWorldPosition(dynamicBody, {50.0F, 0.0F, 0.0F}, dynamicInfo.position) ||
        !world.AddTorque(dynamicBody, {0.0F, 0.0F, 50.0F}))
    {
        return false;
    }

    world.Step(1.0F / 60.0F);
    const auto after = world.GetBodyState(staticBody);
    const auto dynamicAfter = world.GetBodyState(dynamicBody);
    if (!after || !dynamicAfter || after->position != before->position ||
        !PhysicsQuaternion::SameRotation(after->orientation, before->orientation) ||
        after->linearVelocity != before->linearVelocity || after->angularVelocity != before->angularVelocity ||
        dynamicAfter->position.x <= dynamicInfo.position.x || std::abs(dynamicAfter->angularVelocity.z) <= 0.0F)
    {
        return false;
    }

    return world.DestroyBody(staticBody) && !world.GetBodyState(staticBody) &&
           !world.DestroyBody(staticBody, &error) && error.code == PhysicsErrorCode::InvalidHandle &&
           world.DestroyBody(dynamicBody);
}

bool M3B1CollisionTopologyIndependent()
{
    const auto section = DeepRun::Game::BuildSeabedSection({"m3_seabed_01"}, {});
    DeepRun::Game::SeabedProfileConfig profile;
    profile.sampleCount = 321;
    const auto dense = DeepRun::Game::BuildSeabedSection({"m3_seabed_01"}, profile);
    if (!section || !dense || section->collisionBoxes.size() != 57U || section->id != dense->id ||
        section->collisionBoxes != dense->collisionBoxes ||
        section->renderGeometry.primitives[0].vertices.size() == dense->renderGeometry.primitives[0].vertices.size())
        return false;
    float deviation = 0;
    for (std::size_t vertex = 0; vertex < section->renderGeometry.primitives[0].vertices.size(); vertex += 16U)
    {
        const auto& point = section->renderGeometry.primitives[0].vertices[vertex].position;
        float collisionTop = -std::numeric_limits<float>::infinity();
        for (const auto& box : section->collisionBoxes)
        {
            if (std::abs(point.x - box.position.x) <= box.halfExtents.x + 0.001F &&
                box.position.y - box.halfExtents.y <= section->bounds.minimum.y + 0.001F)
            {
                collisionTop = std::max(collisionTop, box.position.y + box.halfExtents.y);
            }
        }
        if (!std::isfinite(collisionTop) || collisionTop < point.y - 0.001F) return false;
        deviation = std::max(deviation, collisionTop - point.y);
    }
    std::cout << "[M3-B.2 evidence] 54 terrain columns + 3 collidable rocks / 160 render cells; sampled vertical error "
              << deviation << " m\n";
    return deviation <= 6.6F;
}

bool M3B1CanonicalSubmarineContact()
{
    using namespace DeepRun::Physics;
    const auto section = DeepRun::Game::BuildSeabedSection({"m3_seabed_01"}, {});
    if (!section) return false;
    DeepRun::Diagnostics::Logger logger;
    PhysicsWorld world(logger);
    if (!world.Initialize()) return false;
    float highestOverInitialVessel = -1000;
    for (const auto& box : section->collisionBoxes)
    {
        if (!world.CreateStaticBoxBody(box).IsValid()) return false;
        if (std::abs(box.position.x - 1.0F) < 51.0F + box.halfExtents.x)
        {
            highestOverInitialVessel = std::max(highestOverInitialVessel, box.position.y + box.halfExtents.y);
        }
    }
    // Same canonical vessel dimensions, mass, position and planar DOFs. Gravity alone drives contact;
    // production buoyancy/hydrodynamics are unchanged. No renderer or GPU is constructed.
    if (-100.0F - 9.7F <= highestOverInitialVessel) return false;
    DynamicBoxBodyCreateInfo info{.halfExtents = {51, 9.7F, 10.5F}, .mass = 12'000'000,
                                .position = {1,-100,0}};
    info.degreesOfFreedom.translationZ = false;
    info.degreesOfFreedom.rotationX = false;
    info.degreesOfFreedom.rotationY = false;
    const auto vessel = world.CreateDynamicBoxBody(info);
    if (!vessel.IsValid()) return false;
    for (int i = 0; i < 1200; ++i)
    {
        world.Step(1.0F / 60.0F);
        const auto state = world.GetBodyState(vessel);
        if (!state || !state->position.IsFinite() || state->position.y < -250 || state->position.z != 0)
            return false;
    }
    const auto state = world.GetBodyState(vessel);
    std::cout << "[M3-B.1 evidence] canonical vessel dropped from Y=-100; resting Y=" << state->position.y
              << ", Vy=" << state->linearVelocity.y << "\n";
    return state->position.y < -185 && state->position.y > -205 && std::abs(state->linearVelocity.y) < 0.1F;
}

bool M3B2RepresentativeTerrainCollisionContacts()
{
    using namespace DeepRun::Physics;
    const auto section = DeepRun::Game::BuildSeabedSection({"m3_seabed_01"}, {});
    if (!section || section->collisionBoxes.size() != 57U) return false;
    DeepRun::Diagnostics::Logger logger;
    PhysicsWorld world(logger);
    if (!world.Initialize()) return false;
    for (const auto& box : section->collisionBoxes)
    {
        if (!world.CreateStaticBoxBody(box).IsValid()) return false;
    }
    const auto drop = [&](const float x, const float startY) {
        const auto body = world.CreateDynamicBoxBody({.halfExtents = {1.0F, 1.0F, 1.0F},
                                                      .mass = 10.0F,
                                                      .position = {x, startY, 0.0F}});
        if (!body.IsValid()) return std::optional<PhysicsBodyState>{};
        for (int step = 0; step < 900; ++step) world.Step(1.0F / 60.0F);
        return world.GetBodyState(body);
    };
    const auto ridge = drop(-100.0F, -80.0F);
    const auto trench = drop(40.0F, -180.0F);
    if (!ridge || !trench || ridge->position.y > -120.0F || ridge->position.y < -140.0F ||
        trench->position.y > -215.0F || trench->position.y < -222.0F)
    {
        return false;
    }

    DynamicBoxBodyCreateInfo crossing;
    crossing.halfExtents = {1.0F, 1.0F, 1.0F};
    crossing.mass = 10.0F;
    crossing.position = {-70.0F, -100.0F, 0.0F};
    crossing.gravityEnabled = false;
    crossing.initialLinearVelocity = {40.0F, 0.0F, 0.0F};
    const auto cliffCrossing = world.CreateDynamicBoxBody(crossing);
    if (!cliffCrossing.IsValid()) return false;
    for (int step = 0; step < 120; ++step) world.Step(1.0F / 60.0F);
    const auto crossingState = world.GetBodyState(cliffCrossing);
    // The body crosses over the drop-off above its lip, proving the floor columns did not turn into a
    // full-height invisible wall. This is not a grounding/damage system.
    return crossingState && crossingState->position.x > -20.0F &&
           std::abs(crossingState->position.y + 100.0F) < 0.01F;
}

bool M3BSeabedSectionGeometryContract()
{
    const auto first = DeepRun::Game::BuildSeabedSection({"seabed-main"}, {});
    const auto second = DeepRun::Game::BuildSeabedSection({"seabed-main"}, {});
    if (!first || !second)
    {
        return false;
    }

    const DeepRun::Game::EnvironmentSection& section = *first;
    const DeepRun::Assets::ModelAsset& model = section.renderGeometry;
    if (section.id.value != "seabed-main" || model.id.Value() != "environment/seabed/seabed-main.section" ||
        model.primitives.size() != 2 || model.nodes.size() != 2 || model.materials.size() != 2 ||
        section.rocks.size() != 7U || !DeepRun::Game::IsSceneLinearBaseColor(model.materials[0]) ||
        !DeepRun::Game::IsSceneLinearBaseColor(model.materials[1]))
    {
        return false;
    }

    const auto& primitive = model.primitives[0];
    const auto& rockPrimitive = model.primitives[1];
    constexpr std::size_t ExpectedCells = 160;
    if (primitive.vertices.size() != ExpectedCells * 16U || primitive.indices.size() != ExpectedCells * 24U ||
        rockPrimitive.vertices.size() != 7U * 38U || rockPrimitive.indices.size() != 7U * 72U ||
        !primitive.hasNormals || !rockPrimitive.hasNormals || primitive.materialIndex != 0U ||
        rockPrimitive.materialIndex != 1U || section.bounds.maximum.y >= 0.0F ||
        model.nodes.front().localToModel.values != DeepRun::Assets::ModelTransform{}.values ||
        model.nodes[1].localToModel.values != DeepRun::Assets::ModelTransform{}.values ||
        model.bounds.minimum.x != section.bounds.minimum.x ||
        model.bounds.minimum.y != section.bounds.minimum.y || model.bounds.minimum.z != section.bounds.minimum.z ||
        model.bounds.maximum.x != section.bounds.maximum.x || model.bounds.maximum.y != section.bounds.maximum.y ||
        model.bounds.maximum.z != section.bounds.maximum.z)
    {
        return false;
    }

    const auto layout = DeepRun::Render::BuildIndexedGeometryLayout(model);
    const auto draws = DeepRun::Render::PrepareModelDraws(model);
    if (!layout || layout->totals.vertexCount != primitive.vertices.size() + rockPrimitive.vertices.size() ||
        layout->totals.indexCount != primitive.indices.size() + rockPrimitive.indices.size() || !draws || draws->size() != 2 ||
        draws->front().modelToWorld.values != DeepRun::Assets::ModelTransform{}.values)
    {
        return false;
    }

    const auto inBounds = [&](const DeepRun::Assets::ModelVector3& position) {
        return position.x >= section.bounds.minimum.x && position.x <= section.bounds.maximum.x &&
               position.y >= section.bounds.minimum.y && position.y <= section.bounds.maximum.y &&
               position.z >= section.bounds.minimum.z && position.z <= section.bounds.maximum.z;
    };
    bool sawSlopeAwareTopNormal = false;
    const auto validPrimitive = [&](const DeepRun::Assets::MeshPrimitiveData& candidate) {
        for (const auto& vertex : candidate.vertices)
        {
            if (!std::isfinite(vertex.position.x) || !std::isfinite(vertex.position.y) ||
                !std::isfinite(vertex.position.z) || !std::isfinite(vertex.normal.x) ||
                !std::isfinite(vertex.normal.y) || !std::isfinite(vertex.normal.z) || !inBounds(vertex.position))
            {
                return false;
            }
        }
        for (std::size_t index = 0; index < candidate.indices.size(); index += 3U)
        {
            const std::uint32_t ia = candidate.indices[index];
            const std::uint32_t ib = candidate.indices[index + 1U];
            const std::uint32_t ic = candidate.indices[index + 2U];
            if (ia >= candidate.vertices.size() || ib >= candidate.vertices.size() || ic >= candidate.vertices.size())
            {
                return false;
            }
            const auto& a = candidate.vertices[ia].position;
            const auto& b = candidate.vertices[ib].position;
            const auto& c = candidate.vertices[ic].position;
            const double ux = static_cast<double>(b.x) - a.x;
            const double uy = static_cast<double>(b.y) - a.y;
            const double uz = static_cast<double>(b.z) - a.z;
            const double vx = static_cast<double>(c.x) - a.x;
            const double vy = static_cast<double>(c.y) - a.y;
            const double vz = static_cast<double>(c.z) - a.z;
            const double areaSquared = (uy * vz - uz * vy) * (uy * vz - uz * vy) +
                                       (uz * vx - ux * vz) * (uz * vx - ux * vz) +
                                       (ux * vy - uy * vx) * (ux * vy - uy * vx);
            if (!std::isfinite(areaSquared) || !(areaSquared > 0.0)) return false;
        }
        return true;
    };
    if (!validPrimitive(primitive) || !validPrimitive(rockPrimitive))
    {
        return false;
    }
    for (std::size_t cell = 0; cell < ExpectedCells; ++cell)
    {
        const auto& topNormal = primitive.vertices[cell * 16U].normal;
        sawSlopeAwareTopNormal = sawSlopeAwareTopNormal || std::abs(topNormal.x) > 0.0001F;
        for (std::size_t vertex = 1; vertex < 4; ++vertex)
        {
            const auto& normal = primitive.vertices[cell * 16U + vertex].normal;
            if (normal.x != topNormal.x || normal.y != topNormal.y || normal.z != topNormal.z || !(normal.y > 0.0F))
            {
                return false;
            }
        }
    }
    if (!sawSlopeAwareTopNormal)
    {
        return false;
    }

    const auto surfaceYAt = [&](const float x) -> std::optional<float> {
        for (std::size_t cell = 0; cell < ExpectedCells; ++cell)
        {
            const auto& position = primitive.vertices[cell * 16U].position;
            if (std::abs(position.x - x) < 0.001F) return position.y;
        }
        return std::nullopt;
    };
    const auto ridge = surfaceYAt(-130.0F);
    const auto floor = surfaceYAt(-250.0F);
    const auto cliffLip = surfaceYAt(-45.0F);
    const auto cliffBase = surfaceYAt(-20.0F);
    const auto trench = surfaceYAt(40.0F);
    if (!ridge || !floor || !cliffLip || !cliffBase || !trench || !(*ridge > *floor) ||
        !(*trench < *floor) || std::abs((*cliffBase - *cliffLip) / 25.0F) < 2.0F ||
        section.rocks[0].id != "ridge-west" || section.rocks[2].id != "ridge-crown" ||
        section.rocks[4].id != "basin-outcrop")
    {
        return false;
    }

    const auto& duplicate = second->renderGeometry.primitives.front();
    if (section.bounds.minimum.x != second->bounds.minimum.x || section.bounds.minimum.y != second->bounds.minimum.y ||
        section.bounds.minimum.z != second->bounds.minimum.z || section.bounds.maximum.x != second->bounds.maximum.x ||
        section.bounds.maximum.y != second->bounds.maximum.y || section.bounds.maximum.z != second->bounds.maximum.z ||
        primitive.indices != duplicate.indices || primitive.vertices.size() != duplicate.vertices.size() ||
        section.rocks != second->rocks || rockPrimitive.indices != second->renderGeometry.primitives[1].indices)
    {
        return false;
    }
    for (std::size_t vertex = 0; vertex < primitive.vertices.size(); ++vertex)
    {
        const auto& a = primitive.vertices[vertex];
        const auto& b = duplicate.vertices[vertex];
        if (a.position.x != b.position.x || a.position.y != b.position.y || a.position.z != b.position.z ||
            a.normal.x != b.normal.x || a.normal.y != b.normal.y || a.normal.z != b.normal.z)
        {
            return false;
        }
    }
    return true;
}

bool M3BSeabedSectionRejectsMalformedInput()
{
    using DeepRun::Game::BuildSeabedSection;
    using DeepRun::Game::EnvironmentSectionId;
    using DeepRun::Game::SeabedProfileConfig;

    const EnvironmentSectionId valid{"seabed_01"};
    const std::array<EnvironmentSectionId, 6> malformedIds{
        EnvironmentSectionId{""}, EnvironmentSectionId{"../seabed"}, EnvironmentSectionId{"seabed/other"},
        EnvironmentSectionId{"seabed\\other"}, EnvironmentSectionId{"seabed.name"}, EnvironmentSectionId{"seabed:01"}};
    for (const auto& id : malformedIds)
    {
        if (id.IsValid() || BuildSeabedSection(id, {}))
        {
            return false;
        }
    }

    const auto rejects = [&](const SeabedProfileConfig& profile) { return !BuildSeabedSection(valid, profile); };
    SeabedProfileConfig sampleCountTooSmall;
    sampleCountTooSmall.sampleCount = 1;
    SeabedProfileConfig sampleCountTooLarge;
    sampleCountTooLarge.sampleCount = 4097;
    SeabedProfileConfig invertedRange;
    invertedRange.maxX = invertedRange.minX;
    SeabedProfileConfig nonFinite;
    nonFinite.controlPoints[3].yMeters = std::numeric_limits<float>::infinity();
    SeabedProfileConfig missingEndpoint;
    missingEndpoint.controlPoints.back().xMeters = 399.0F;
    SeabedProfileConfig reversedKnots;
    reversedKnots.controlPoints[4].xMeters = reversedKnots.controlPoints[3].xMeters;
    SeabedProfileConfig emptyKnots;
    emptyKnots.controlPoints.clear();
    SeabedProfileConfig zeroThickness;
    zeroThickness.zThicknessMeters = 0.0F;
    SeabedProfileConfig fillAtSurface;
    fillAtSurface.fillBottomYMeters = -100.0F;
    SeabedProfileConfig unrepresentableSpan;
    unrepresentableSpan.minX = -std::numeric_limits<float>::max();
    unrepresentableSpan.maxX = std::numeric_limits<float>::max();
    return rejects(sampleCountTooSmall) && rejects(sampleCountTooLarge) && rejects(invertedRange) && rejects(nonFinite) &&
           rejects(missingEndpoint) && rejects(reversedKnots) && rejects(emptyKnots) && rejects(zeroThickness) &&
           rejects(fillAtSurface) && rejects(unrepresentableSpan);
}

bool M3GBoundedUnderwaterFloraFieldContract()
{
    using namespace DeepRun;
    Game::SeabedProfileConfig profile;
    const auto first = Game::BuildUnderwaterFloraField({"seabed-main"}, profile, 0.0F);
    const auto second = Game::BuildUnderwaterFloraField({"seabed-main"}, profile, 0.0F);
    if (!first || !second || first->patchCount != Game::M3UnderwaterFloraPatchCount ||
        first->plants.size() != Game::M3UnderwaterFloraPlantCount ||
        first->renderGeometry.id.Value() != "environment/flora/seabed-main.field" ||
        first->renderGeometry.materials.size() != 1U || first->renderGeometry.primitives.size() != 1U ||
        first->renderGeometry.nodes.size() != 1U)
    {
        return false;
    }

    const Assets::ModelAsset& model = first->renderGeometry;
    const Assets::MeshPrimitiveData& primitive = model.primitives.front();
    const auto layout = Render::BuildIndexedGeometryLayout(model);
    const auto draws = Render::PrepareModelDraws(model);
    if (!layout || !draws || draws->size() != 1U || layout->totals.primitiveCount != 1U ||
        primitive.vertices.size() != 960U || primitive.indices.size() != 1'440U ||
        primitive.indices.size() % 3U != 0U || primitive.indices.size() / 3U != 480U ||
        primitive.indices.size() / 3U > Game::M3UnderwaterFloraTriangleBudget ||
        layout->totals.vertexCount != primitive.vertices.size() || layout->totals.indexCount != primitive.indices.size() ||
        !primitive.hasNormals || primitive.materialIndex != 0U ||
        model.nodes.front().primitiveIndices != std::vector<std::size_t>{0U} ||
        model.bounds.minimum.x != primitive.localBounds.minimum.x || model.bounds.minimum.y != primitive.localBounds.minimum.y ||
        model.bounds.minimum.z != primitive.localBounds.minimum.z || model.bounds.maximum.x != primitive.localBounds.maximum.x ||
        model.bounds.maximum.y != primitive.localBounds.maximum.y || model.bounds.maximum.z != primitive.localBounds.maximum.z)
    {
        return false;
    }
    const Assets::ModelMaterialData& material = model.materials.front();
    if (material.metallicFactor != 0.0F || material.roughnessFactor != 0.95F || material.baseColorFactor[3] != 1.0F ||
        !Game::IsSceneLinearBaseColor(material))
    {
        return false;
    }

    const auto inBounds = [&](const Assets::ModelVector3& position) {
        return position.x >= model.bounds.minimum.x && position.x <= model.bounds.maximum.x &&
               position.y >= model.bounds.minimum.y && position.y <= model.bounds.maximum.y &&
               position.z >= model.bounds.minimum.z && position.z <= model.bounds.maximum.z;
    };
    for (const Assets::MeshVertex& vertex : primitive.vertices)
    {
        const float normalLengthSquared = vertex.normal.x * vertex.normal.x + vertex.normal.y * vertex.normal.y +
                                          vertex.normal.z * vertex.normal.z;
        if (!std::isfinite(vertex.position.x) || !std::isfinite(vertex.position.y) || !std::isfinite(vertex.position.z) ||
            !std::isfinite(vertex.normal.x) || !std::isfinite(vertex.normal.y) || !std::isfinite(vertex.normal.z) ||
            std::abs(normalLengthSquared - 1.0F) > 1.0e-6F || vertex.normal.z != 1.0F || !inBounds(vertex.position))
        {
            return false;
        }
    }
    for (std::size_t index = 0U; index < primitive.indices.size(); index += 3U)
    {
        const std::uint32_t ia = primitive.indices[index];
        const std::uint32_t ib = primitive.indices[index + 1U];
        const std::uint32_t ic = primitive.indices[index + 2U];
        if (ia >= primitive.vertices.size() || ib >= primitive.vertices.size() || ic >= primitive.vertices.size())
        {
            return false;
        }
        const auto& a = primitive.vertices[ia].position;
        const auto& b = primitive.vertices[ib].position;
        const auto& c = primitive.vertices[ic].position;
        const double ux = static_cast<double>(b.x) - a.x;
        const double uy = static_cast<double>(b.y) - a.y;
        const double vx = static_cast<double>(c.x) - a.x;
        const double vy = static_cast<double>(c.y) - a.y;
        const double crossZ = ux * vy - uy * vx;
        if (!std::isfinite(crossZ) || !(crossZ > 0.0))
        {
            return false;
        }
    }

    std::array<std::uint32_t, Game::M3UnderwaterFloraPatchCount> patchCounts{};
    bool sawHeightVariation = false;
    bool sawWidthVariation = false;
    for (std::size_t index = 0U; index < first->plants.size(); ++index)
    {
        const Game::UnderwaterFloraPlant& plant = first->plants[index];
        const Game::UnderwaterFloraPlant& duplicate = second->plants[index];
        const auto profileY = Game::SampleSeabedProfileY(profile, plant.rootPosition.x);
        if (!profileY || plant.patchIndex >= patchCounts.size() || plant.segmentCount != Game::M3UnderwaterFloraSegmentsPerPlant ||
            !std::isfinite(plant.rootPosition.x) || !std::isfinite(plant.rootPosition.y) || !std::isfinite(plant.rootPosition.z) ||
            std::abs(plant.rootPosition.y - *profileY) > Game::M3UnderwaterFloraRootContactToleranceMeters ||
            plant.rootPosition.y >= 0.0F || plant.rootPosition.y + plant.heightMeters >= 0.0F ||
            plant.heightMeters < 2.5F || plant.heightMeters > 7.0F ||
            plant.halfWidthMeters < 0.18F || plant.halfWidthMeters > 0.45F ||
            plant.rootPosition.x < profile.minX || plant.rootPosition.x > profile.maxX ||
            plant.rootPosition.z <= 0.5F * profile.zThicknessMeters ||
            plant.rootPosition.z > 0.5F * profile.zThicknessMeters + 0.41F ||
            plant.rootPosition.x != duplicate.rootPosition.x || plant.rootPosition.y != duplicate.rootPosition.y ||
            plant.rootPosition.z != duplicate.rootPosition.z || plant.heightMeters != duplicate.heightMeters ||
            plant.halfWidthMeters != duplicate.halfWidthMeters || plant.bendMeters != duplicate.bendMeters ||
            plant.patchIndex != duplicate.patchIndex || plant.segmentCount != duplicate.segmentCount)
        {
            return false;
        }
        ++patchCounts[plant.patchIndex];
        sawHeightVariation = sawHeightVariation || plant.heightMeters != first->plants.front().heightMeters;
        sawWidthVariation = sawWidthVariation || plant.halfWidthMeters != first->plants.front().halfWidthMeters;
    }
    if (patchCounts != std::array<std::uint32_t, 3>{18U, 22U, 20U} || !sawHeightVariation || !sawWidthVariation ||
        Game::SampleSeabedProfileY(profile, profile.minX - 0.01F) ||
        Game::BuildUnderwaterFloraField({"seabed-main"}, profile, std::numeric_limits<float>::infinity()))
    {
        return false;
    }

    const auto& duplicatePrimitive = second->renderGeometry.primitives.front();
    if (primitive.indices != duplicatePrimitive.indices || primitive.vertices.size() != duplicatePrimitive.vertices.size())
    {
        return false;
    }
    for (std::size_t index = 0U; index < primitive.vertices.size(); ++index)
    {
        const Assets::MeshVertex& a = primitive.vertices[index];
        const Assets::MeshVertex& b = duplicatePrimitive.vertices[index];
        if (a.position.x != b.position.x || a.position.y != b.position.y || a.position.z != b.position.z ||
            a.normal.x != b.normal.x || a.normal.y != b.normal.y || a.normal.z != b.normal.z)
        {
            return false;
        }
    }
    return true;
}

bool M3HBoundedUnderwaterIceFieldContract()
{
    using namespace DeepRun;
    const auto first = Game::BuildUnderwaterIceField({"seabed-main"}, 0.0F);
    const auto second = Game::BuildUnderwaterIceField({"seabed-main"}, 0.0F);
    if (!first || !second || first->formations.size() != Game::M3UnderwaterIceFormationCount ||
        first->collisionBoxes.size() != Game::M3UnderwaterIceCollisionCount ||
        first->renderGeometry.id.Value() != "environment/ice/seabed-main.field" ||
        first->renderGeometry.materials.size() != 1U || first->renderGeometry.primitives.size() != 1U ||
        first->renderGeometry.nodes.size() != 1U || first->collisionBoxes != second->collisionBoxes)
    {
        return false;
    }

    const Assets::ModelAsset& model = first->renderGeometry;
    const Assets::MeshPrimitiveData& primitive = model.primitives.front();
    const auto layout = Render::BuildIndexedGeometryLayout(model);
    const auto draws = Render::PrepareModelDraws(model);
    if (!layout || !draws || draws->size() != 1U || layout->totals.primitiveCount != 1U ||
        primitive.vertices.size() != 126U || primitive.indices.size() != 240U ||
        primitive.indices.size() % 3U != 0U || primitive.indices.size() / 3U != 80U ||
        primitive.indices.size() / 3U > Game::M3UnderwaterIceTriangleBudget ||
        layout->totals.vertexCount != primitive.vertices.size() || layout->totals.indexCount != primitive.indices.size() ||
        !primitive.hasNormals || primitive.materialIndex != 0U ||
        model.nodes.front().primitiveIndices != std::vector<std::size_t>{0U})
    {
        return false;
    }
    const Assets::ModelMaterialData& material = model.materials.front();
    if (material.baseColorFactor != std::array<float, 4>{0.24F, 0.46F, 0.54F, 1.0F} ||
        material.metallicFactor != 0.0F || material.roughnessFactor != 0.82F ||
        !Game::IsSceneLinearBaseColor(material))
    {
        return false;
    }

    const auto inBounds = [&](const Assets::ModelVector3& position) {
        return position.x >= model.bounds.minimum.x && position.x <= model.bounds.maximum.x &&
               position.y >= model.bounds.minimum.y && position.y <= model.bounds.maximum.y &&
               position.z >= model.bounds.minimum.z && position.z <= model.bounds.maximum.z;
    };
    for (const Assets::MeshVertex& vertex : primitive.vertices)
    {
        const float normalLengthSquared = vertex.normal.x * vertex.normal.x + vertex.normal.y * vertex.normal.y +
                                          vertex.normal.z * vertex.normal.z;
        if (!std::isfinite(vertex.position.x) || !std::isfinite(vertex.position.y) || !std::isfinite(vertex.position.z) ||
            !std::isfinite(vertex.normal.x) || !std::isfinite(vertex.normal.y) || !std::isfinite(vertex.normal.z) ||
            std::abs(normalLengthSquared - 1.0F) > 1.0e-5F || !inBounds(vertex.position))
        {
            return false;
        }
    }
    for (std::size_t index = 0U; index < primitive.indices.size(); index += 3U)
    {
        const std::uint32_t ia = primitive.indices[index];
        const std::uint32_t ib = primitive.indices[index + 1U];
        const std::uint32_t ic = primitive.indices[index + 2U];
        if (ia >= primitive.vertices.size() || ib >= primitive.vertices.size() || ic >= primitive.vertices.size())
        {
            return false;
        }
        const auto& a = primitive.vertices[ia].position;
        const auto& b = primitive.vertices[ib].position;
        const auto& c = primitive.vertices[ic].position;
        const double ux = static_cast<double>(b.x) - a.x;
        const double uy = static_cast<double>(b.y) - a.y;
        const double uz = static_cast<double>(b.z) - a.z;
        const double vx = static_cast<double>(c.x) - a.x;
        const double vy = static_cast<double>(c.y) - a.y;
        const double vz = static_cast<double>(c.z) - a.z;
        const double areaSquared = (uy * vz - uz * vy) * (uy * vz - uz * vy) +
                                   (uz * vx - ux * vz) * (uz * vx - ux * vz) +
                                   (ux * vy - uy * vx) * (ux * vy - uy * vx);
        if (!std::isfinite(areaSquared) || !(areaSquared > 0.0))
        {
            return false;
        }
    }

    struct ExpectedFormation final
    {
        std::string_view id;
        Game::UnderwaterIceFormationType type;
        Assets::ModelVector3 position;
        Assets::ModelVector3 renderHalfExtents;
        bool hasCoarseCollision;
    };
    constexpr std::array<ExpectedFormation, 3> expected{
        ExpectedFormation{"surface-shelf-west", Game::UnderwaterIceFormationType::SurfaceShelf,
                          {-225.0F, -14.0F, 0.0F}, {44.0F, 15.0F, 4.0F}, true},
        ExpectedFormation{"hanging-formation-central", Game::UnderwaterIceFormationType::HangingFormation,
                          {-85.0F, -30.0F, 0.0F}, {18.0F, 30.0F, 3.5F}, false},
        ExpectedFormation{"iceberg-keel-east", Game::UnderwaterIceFormationType::IcebergKeel,
                          {230.0F, -42.0F, 0.0F}, {25.0F, 34.0F, 4.5F}, true}};
    std::size_t collidableCount = 0U;
    for (std::size_t index = 0U; index < expected.size(); ++index)
    {
        const Game::UnderwaterIceInstance& formation = first->formations[index];
        const Game::UnderwaterIceInstance& duplicate = second->formations[index];
        const ExpectedFormation& wanted = expected[index];
        if (formation.id != wanted.id || formation.type != wanted.type || formation.position.x != wanted.position.x ||
            formation.position.y != wanted.position.y || formation.position.z != wanted.position.z ||
            formation.renderHalfExtents.x != wanted.renderHalfExtents.x ||
            formation.renderHalfExtents.y != wanted.renderHalfExtents.y ||
            formation.renderHalfExtents.z != wanted.renderHalfExtents.z || formation.rotationRadians != 0.0F ||
            formation.hasCoarseCollision != wanted.hasCoarseCollision || formation.id != duplicate.id ||
            formation.type != duplicate.type || formation.position.x != duplicate.position.x ||
            formation.position.y != duplicate.position.y || formation.position.z != duplicate.position.z ||
            formation.renderHalfExtents.x != duplicate.renderHalfExtents.x ||
            formation.renderHalfExtents.y != duplicate.renderHalfExtents.y ||
            formation.renderHalfExtents.z != duplicate.renderHalfExtents.z ||
            formation.coarseCollision != duplicate.coarseCollision)
        {
            return false;
        }
        collidableCount += formation.hasCoarseCollision ? 1U : 0U;
    }
    if (collidableCount != Game::M3UnderwaterIceCollisionCount || model.bounds.minimum.y >= 0.0F ||
        Game::BuildUnderwaterIceField({"bad/id"}, 0.0F) ||
        Game::BuildUnderwaterIceField({"seabed-main"}, std::numeric_limits<float>::infinity()))
    {
        return false;
    }

    const auto& duplicatePrimitive = second->renderGeometry.primitives.front();
    if (primitive.indices != duplicatePrimitive.indices || primitive.vertices.size() != duplicatePrimitive.vertices.size())
    {
        return false;
    }
    for (std::size_t index = 0U; index < primitive.vertices.size(); ++index)
    {
        const Assets::MeshVertex& a = primitive.vertices[index];
        const Assets::MeshVertex& b = duplicatePrimitive.vertices[index];
        if (a.position.x != b.position.x || a.position.y != b.position.y || a.position.z != b.position.z ||
            a.normal.x != b.normal.x || a.normal.y != b.normal.y || a.normal.z != b.normal.z)
        {
            return false;
        }
    }
    return true;
}

bool M3HIceRenderAndCollisionRepresentationsAreIndependent()
{
    const auto field = DeepRun::Game::BuildUnderwaterIceField({"seabed-main"}, 0.0F);
    if (!field || field->formations.size() != DeepRun::Game::M3UnderwaterIceFormationCount ||
        field->collisionBoxes.size() != DeepRun::Game::M3UnderwaterIceCollisionCount)
    {
        return false;
    }
    std::size_t collisionIndex = 0U;
    for (const DeepRun::Game::UnderwaterIceInstance& formation : field->formations)
    {
        if (!formation.hasCoarseCollision)
        {
            if (formation.coarseCollision != DeepRun::Physics::StaticBoxBodyCreateInfo{}) return false;
            continue;
        }
        if (collisionIndex >= field->collisionBoxes.size() ||
            field->collisionBoxes[collisionIndex] != formation.coarseCollision ||
            formation.coarseCollision.position.x != formation.position.x ||
            formation.coarseCollision.position.y != formation.position.y ||
            formation.coarseCollision.position.z != formation.position.z ||
            (formation.coarseCollision.halfExtents.x == formation.renderHalfExtents.x &&
             formation.coarseCollision.halfExtents.y == formation.renderHalfExtents.y &&
             formation.coarseCollision.halfExtents.z == formation.renderHalfExtents.z))
        {
            return false;
        }
        ++collisionIndex;
    }
    return collisionIndex == field->collisionBoxes.size();
}

bool M3HIceCoarseCollisionStopsDynamicBox()
{
    using namespace DeepRun::Physics;
    const auto field = DeepRun::Game::BuildUnderwaterIceField({"seabed-main"}, 0.0F);
    if (!field || field->collisionBoxes.size() != 2U) return false;
    DeepRun::Diagnostics::Logger logger;
    PhysicsWorld world(logger);
    if (!world.Initialize()) return false;
    for (const StaticBoxBodyCreateInfo& box : field->collisionBoxes)
    {
        if (!world.CreateStaticBoxBody(box).IsValid()) return false;
    }
    const PhysicsBodyHandle falling = world.CreateDynamicBoxBody({
        .halfExtents = {1.0F, 1.0F, 1.0F}, .mass = 10.0F, .position = {-225.0F, 20.0F, 0.0F}});
    if (!falling.IsValid()) return false;
    for (int step = 0; step < 600; ++step) world.Step(1.0F / 60.0F);
    const auto state = world.GetBodyState(falling);
    return state && state->position.IsFinite() && state->linearVelocity.IsFinite() &&
           std::abs(state->position.x + 225.0F) < 0.1F && state->position.y > 0.75F && state->position.y < 1.5F &&
           std::abs(state->linearVelocity.y) < 0.1F;
}

bool SimulationHasNoGerstnerRenderPresentationDependency();

bool M3H1BoundedUnderwaterFaunaFieldContract()
{
    using namespace DeepRun;
    const auto first = Game::BuildUnderwaterFaunaField({"seabed-main"}, 0.0F);
    const auto second = Game::BuildUnderwaterFaunaField({"seabed-main"}, 0.0F);
    if (!first || !second || first->fish.size() != Game::M3UnderwaterFishCount ||
        first->renderGeometry.id.Value() != "environment/fauna/seabed-main.fish-school" ||
        first->renderGeometry.materials.size() != 1U || first->renderGeometry.primitives.size() != 1U ||
        first->renderGeometry.nodes.size() != 1U ||
        first->presentation.travelMinimumX != -160.0F || first->presentation.travelMaximumX != 160.0F ||
        first->presentation.centerY != -75.0F || first->presentation.horizontalSpeedMetersPerSecond != 5.0F)
    {
        return false;
    }

    const Assets::ModelAsset& model = first->renderGeometry;
    const Assets::MeshPrimitiveData& primitive = model.primitives.front();
    const auto layout = Render::BuildIndexedGeometryLayout(model);
    const auto draws = Render::PrepareModelDraws(model);
    if (!layout || !draws || draws->size() != 1U || layout->totals.primitiveCount != 1U ||
        primitive.vertices.size() != 168U || primitive.indices.size() != 216U ||
        primitive.indices.size() / 3U != Game::M3UnderwaterFishCount * Game::M3UnderwaterFishTrianglesPerFish ||
        primitive.indices.size() / 3U > Game::M3UnderwaterFishTriangleBudget || !primitive.hasNormals ||
        primitive.materialIndex != 0U || model.nodes.front().primitiveIndices != std::vector<std::size_t>{0U} ||
        !std::isfinite(model.bounds.minimum.x) || !std::isfinite(model.bounds.minimum.y) ||
        !std::isfinite(model.bounds.minimum.z) || !std::isfinite(model.bounds.maximum.x) ||
        !std::isfinite(model.bounds.maximum.y) || !std::isfinite(model.bounds.maximum.z) ||
        model.bounds.minimum.x >= model.bounds.maximum.x ||
        model.bounds.minimum.y >= model.bounds.maximum.y || model.bounds.minimum.z >= model.bounds.maximum.z ||
        model.bounds.minimum.z < Game::M3UnderwaterFishMinimumLocalZ ||
        model.bounds.maximum.z > Game::M3UnderwaterFishMaximumLocalZ || model.bounds.minimum.y >= model.bounds.maximum.y)
    {
        return false;
    }
    const Assets::ModelMaterialData& material = model.materials.front();
    if (material.baseColorFactor != std::array<float, 4>{0.22F, 0.34F, 0.38F, 1.0F} ||
        material.metallicFactor != 0.0F || material.roughnessFactor != 0.88F ||
        !Game::IsSceneLinearBaseColor(material))
    {
        return false;
    }

    for (const Game::UnderwaterFish& fish : first->fish)
    {
        if (!std::isfinite(fish.localPosition.x) || !std::isfinite(fish.localPosition.y) ||
            !std::isfinite(fish.localPosition.z) || fish.localPosition.z < Game::M3UnderwaterFishMinimumLocalZ ||
            fish.localPosition.z > Game::M3UnderwaterFishMaximumLocalZ ||
            fish.bodyLengthMeters < Game::M3UnderwaterFishMinimumBodyLengthMeters ||
            fish.bodyLengthMeters > Game::M3UnderwaterFishMaximumBodyLengthMeters ||
            fish.bodyHeightMeters < Game::M3UnderwaterFishMinimumBodyHeightMeters ||
            fish.bodyHeightMeters > Game::M3UnderwaterFishMaximumBodyHeightMeters ||
            !std::isfinite(fish.headingRadians))
        {
            return false;
        }
    }
    for (std::size_t index = 0U; index < first->fish.size(); ++index)
    {
        const Game::UnderwaterFish& a = first->fish[index];
        const Game::UnderwaterFish& b = second->fish[index];
        if (a.localPosition.x != b.localPosition.x || a.localPosition.y != b.localPosition.y ||
            a.localPosition.z != b.localPosition.z || a.bodyLengthMeters != b.bodyLengthMeters ||
            a.bodyHeightMeters != b.bodyHeightMeters || a.headingRadians != b.headingRadians)
        {
            return false;
        }
    }
    for (const Assets::MeshVertex& vertex : primitive.vertices)
    {
        const float normalLengthSquared = vertex.normal.x * vertex.normal.x + vertex.normal.y * vertex.normal.y +
                                          vertex.normal.z * vertex.normal.z;
        if (!std::isfinite(vertex.position.x) || !std::isfinite(vertex.position.y) ||
            !std::isfinite(vertex.position.z) || std::abs(normalLengthSquared - 1.0F) > 1.0e-5F ||
            vertex.normal.z != 1.0F || vertex.position.x < model.bounds.minimum.x ||
            vertex.position.x > model.bounds.maximum.x || vertex.position.y < model.bounds.minimum.y ||
            vertex.position.y > model.bounds.maximum.y || vertex.position.z < model.bounds.minimum.z ||
            vertex.position.z > model.bounds.maximum.z)
        {
            return false;
        }
    }
    for (std::size_t index = 0U; index < primitive.indices.size(); index += 3U)
    {
        const std::uint32_t ia = primitive.indices[index];
        const std::uint32_t ib = primitive.indices[index + 1U];
        const std::uint32_t ic = primitive.indices[index + 2U];
        if (ia >= primitive.vertices.size() || ib >= primitive.vertices.size() || ic >= primitive.vertices.size())
        {
            return false;
        }
        const auto& a = primitive.vertices[ia].position;
        const auto& b = primitive.vertices[ib].position;
        const auto& c = primitive.vertices[ic].position;
        const double ux = static_cast<double>(b.x) - a.x;
        const double uy = static_cast<double>(b.y) - a.y;
        const double vx = static_cast<double>(c.x) - a.x;
        const double vy = static_cast<double>(c.y) - a.y;
        if (!std::isfinite(ux * vy - uy * vx) || std::abs(ux * vy - uy * vx) <= 1.0e-6)
        {
            return false;
        }
    }
    return primitive.indices == second->renderGeometry.primitives.front().indices &&
           primitive.vertices.size() == second->renderGeometry.primitives.front().vertices.size();
}

bool M3H1UnderwaterFaunaPresentationMotion()
{
    const auto field = DeepRun::Game::BuildUnderwaterFaunaField({"seabed-main"}, 0.0F);
    if (!field)
    {
        return false;
    }
    const auto atStart = DeepRun::Game::EvaluateUnderwaterFishSchoolPresentation(field->presentation, 0.0);
    const auto atStartAgain = DeepRun::Game::EvaluateUnderwaterFishSchoolPresentation(field->presentation, 0.0);
    const auto atOneSecond = DeepRun::Game::EvaluateUnderwaterFishSchoolPresentation(field->presentation, 1.0);
    const auto justBeforeWrap = DeepRun::Game::EvaluateUnderwaterFishSchoolPresentation(field->presentation, 63.999);
    const auto atWrap = DeepRun::Game::EvaluateUnderwaterFishSchoolPresentation(field->presentation, 64.0);
    const auto atLargeFiniteTime = DeepRun::Game::EvaluateUnderwaterFishSchoolPresentation(
        field->presentation, 1.0e300);
    if (!atStart || !atStartAgain || !atOneSecond || !justBeforeWrap || !atWrap || !atLargeFiniteTime ||
        atStart->values != atStartAgain->values || atStart->values[12] != -160.0F ||
        atStart->values[13] != -75.0F || atOneSecond->values[12] <= atStart->values[12] ||
        atOneSecond->values[13] == atStart->values[13] || atStart->values[14] != 0.0F ||
        justBeforeWrap->values[12] < -160.0F || justBeforeWrap->values[12] >= 160.0F ||
        atWrap->values[12] != -160.0F || justBeforeWrap->values[13] < -76.5F ||
        justBeforeWrap->values[13] > -73.5F ||
        !std::ranges::all_of(atLargeFiniteTime->values, [](const float value) { return std::isfinite(value); }))
    {
        return false;
    }
    const auto invalidTime = DeepRun::Game::EvaluateUnderwaterFishSchoolPresentation(
        field->presentation, std::numeric_limits<double>::infinity());
    const auto negativeTime = DeepRun::Game::EvaluateUnderwaterFishSchoolPresentation(field->presentation, -1.0);
    auto invalidParameters = field->presentation;
    invalidParameters.travelMaximumX = invalidParameters.travelMinimumX;
    return !invalidTime && !negativeTime && !DeepRun::Game::EvaluateUnderwaterFishSchoolPresentation(
                                  invalidParameters, 0.0);
}

bool M3H1FaunaStaysPresentationOnly()
{
    const std::filesystem::path faunaRoot =
        std::filesystem::path(DEEPRUN_SOURCE_ROOT) / "Game" / "Environment";
    const std::array<std::filesystem::path, 2> files{{
        faunaRoot / "UnderwaterFaunaField.h", faunaRoot / "UnderwaterFaunaField.cpp"}};
    const std::array<std::string_view, 12> forbidden{{
        "simulation/", "simulationtime", "waterbody", "waterwavefield", "buoyancysystem", "physicsworld",
        "acoustics", "<jolt/", "jph::", "engine/render", "d3d12", "addforceatworldposition"}};
    for (const auto& file : files)
    {
        std::ifstream input(file, std::ios::binary);
        if (!input)
        {
            return false;
        }
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
    // Keep the repository-wide Simulation -> Render/Gerstner guard active for this presentation slice too.
    return SimulationHasNoGerstnerRenderPresentationDependency();
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
    // Engine/Physics must not know about GPU models or domain-specific marine systems (ADR-0007 / F2).
    const std::filesystem::path physicsRoot = std::filesystem::path(DEEPRUN_SOURCE_ROOT) / "Engine" / "Physics";
    return ScanSourceDirectoryForForbiddenPatterns(
        physicsRoot, {"gpumodel", "d3d12", "waterbody", "buoyancy", "hydrodrag", "simulation/marine"});
}

bool PhysicsPublicHeadersHaveNoJoltDependency()
{
    const std::filesystem::path physicsRoot = std::filesystem::path(DEEPRUN_SOURCE_ROOT) / "Engine" / "Physics";
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(physicsRoot))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".h")
        {
            continue;
        }
        std::ifstream input(entry.path(), std::ios::binary);
        std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        std::ranges::transform(contents, contents.begin(), [](const unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        if (contents.find("<jolt/") != std::string::npos || contents.find("jph::") != std::string::npos)
        {
            return false;
        }
    }
    return true;
}

bool SimulationMarineHasNoPhysicsOrRenderDependency()
{
    // Marine simulation must stay free of rigid-body lifecycle/application APIs and presentation/assets
    // (M2 D1/E2). DeepRun-owned PhysicsTypes are allowed only as generic pose/vector value types.
    const std::filesystem::path marineRoot =
        std::filesystem::path(DEEPRUN_SOURCE_ROOT) / "Simulation" / "Marine";
    return ScanSourceDirectoryForForbiddenPatterns(
        marineRoot,
        {"<jolt/", "jph::", "physicsbodyhandle", "physicsbodystate", "physicsworld", "addforceatworldposition",
         "modelasset", "modelvector3", "gpumodelhandle", "engine/render", "render/", "d3d12",
         "directxmath", "game/", "12'000'000", "m2prototypemass", "submarinemodelpath",
         "collision bounds"});
}

bool SimulationHasNoGerstnerRenderPresentationDependency()
{
    // M3-E.1 and M3-H.1 have separate Game/Marine contracts; Simulation must never acquire Render's
    // validation API or the presentation fish field's visual formula.
    const std::filesystem::path simulationRoot = std::filesystem::path(DEEPRUN_SOURCE_ROOT) / "Simulation";
    return ScanSourceDirectoryForForbiddenPatterns(
        simulationRoot,
        {"engine/render/gerstnersurface.h", "gerstnersurfacepresentationparameters",
         "evaluategerstnersurfacepresentation"}) &&
           ScanSourceDirectoryForForbiddenPatterns(
               std::filesystem::path(DEEPRUN_SOURCE_ROOT) / "Engine" / "Render",
               {"simulation/", "marine::", "waterwavefielddefinition", "waterwavecomponent"});
}

bool M3FWaveBuoyancyHasNoRenderDependency()
{
    // The explicit M3-F physical consumer is still pure Marine code: it may query WaterBody but never
    // includes or names a presentation formula, renderer, shader API, or presentation clock.
    const std::filesystem::path marineRoot =
        std::filesystem::path(DEEPRUN_SOURCE_ROOT) / "Simulation" / "Marine";
    return ScanSourceDirectoryForForbiddenPatterns(
        marineRoot,
        {"engine/render/", "d3d12", "gerstner", "presentationtime"});
}

bool M3GFloraStaysPresentationOnly()
{
    // M3-G's pure Game field may consume the authored profile and ModelAsset values, but it must never gain
    // physics lifecycle, surface-query, simulation, or GPU-renderer authority.
    const std::filesystem::path floraRoot = std::filesystem::path(DEEPRUN_SOURCE_ROOT) / "Game" / "Environment";
    const std::array<std::filesystem::path, 2> files{{
        floraRoot / "UnderwaterFloraField.h", floraRoot / "UnderwaterFloraField.cpp"}};
    const std::array<std::string_view, 10> forbidden{{
        "physicsworld", "physicsbodyhandle", "physicsbodystate", "createstaticboxbody", "createdynamicboxbody",
        "addforceatworldposition", "waterbody", "samplewavesurface", "engine/render/", "d3d12"}};
    for (const std::filesystem::path& file : files)
    {
        std::ifstream input(file, std::ios::binary);
        if (!input)
        {
            return false;
        }
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

bool M3HIceStaysGameOwnedAndStatic()
{
    // M3-H may carry ModelAsset and StaticBoxBodyCreateInfo values, but the field itself must not acquire
    // renderer, water/wave, clock, simulation, or PhysicsWorld lifecycle dependencies.
    const std::filesystem::path iceRoot = std::filesystem::path(DEEPRUN_SOURCE_ROOT) / "Game" / "Environment";
    const std::array<std::filesystem::path, 2> files{{
        iceRoot / "UnderwaterIceField.h", iceRoot / "UnderwaterIceField.cpp"}};
    const std::array<std::string_view, 11> forbidden{{
        "d3d12renderer", "engine/render/", "waterbody", "samplewavesurface", "presentationtime",
        "simulationtime", "simulation/marine", "physicsworld", "createstaticboxbody",
        "createdynamicboxbody", "addforceatworldposition"}};
    for (const std::filesystem::path& file : files)
    {
        std::ifstream input(file, std::ios::binary);
        if (!input)
        {
            return false;
        }
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

bool HydroDragFilesHaveOnlyPureMarineDependencies()
{
    const std::filesystem::path marineRoot =
        std::filesystem::path(DEEPRUN_SOURCE_ROOT) / "Simulation" / "Marine";
    const std::filesystem::path files[] = {
        marineRoot / "HydroDragComponent.h",
        marineRoot / "HydroDragSystem.h",
        marineRoot / "HydroDragSystem.cpp"};
    const std::string_view forbidden[] = {
        "<jolt/", "jph::", "physicsworld", "physicsbodyhandle", "addforceatworldposition", "game/",
        "engine/render", "d3d12", "modelasset", "buoyancyresult"};

    for (const std::filesystem::path& path : files)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input)
        {
            return false;
        }
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

bool PropulsionFilesHaveOnlyPureMarineDependencies()
{
    const std::filesystem::path marineRoot =
        std::filesystem::path(DEEPRUN_SOURCE_ROOT) / "Simulation" / "Marine";
    const std::filesystem::path files[] = {
        marineRoot / "PropulsionComponent.h",
        marineRoot / "PropulsionSystem.h",
        marineRoot / "PropulsionSystem.cpp"};
    const std::string_view forbidden[] = {
        "<jolt/", "jph::", "physicsworld", "physicsbodyhandle", "addforceatworldposition", "addtorque",
        "game/", "engine/render", "d3d12", "modelasset", "inputaction", "xinput", "hydrodragresult",
        "buoyancyresult"};

    for (const std::filesystem::path& path : files)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input)
        {
            return false;
        }
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

bool ControlSurfaceFilesHaveOnlyPureMarineDependencies()
{
    const std::filesystem::path marineRoot =
        std::filesystem::path(DEEPRUN_SOURCE_ROOT) / "Simulation" / "Marine";
    const std::filesystem::path files[] = {
        marineRoot / "ControlSurfaceComponent.h",
        marineRoot / "ControlSurfaceSystem.h",
        marineRoot / "ControlSurfaceSystem.cpp"};
    const std::string_view forbidden[] = {
        "<jolt/", "jph::", "physicsworld", "physicsbodyhandle", "addforceatworldposition", "addtorque",
        "game/", "engine/render", "d3d12", "modelasset", "propulsionresult", "hydrodragresult",
        "buoyancyresult", "inputaction", "xinput"};

    for (const std::filesystem::path& path : files)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input)
        {
            return false;
        }
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

bool EngineHasNoMarineKnowledge()
{
    // The generic engine must not become aware of marine simulation (M2 Slice D1).
    const std::filesystem::path engineRoot = std::filesystem::path(DEEPRUN_SOURCE_ROOT) / "Engine";
    return ScanSourceDirectoryForForbiddenPatterns(
        engineRoot, {"waterbody", "marine", "buoyancy", "hydrodrag", "submarine"});
}

bool EngineRenderHasNoWaterSemantics()
{
    // The renderer stays generic (M2 Slice D2): it knows rectangles and colors, never water. Game owns the
    // presentation semantics; the clear-rect API must not grow marine vocabulary.
    const std::filesystem::path renderRoot = std::filesystem::path(DEEPRUN_SOURCE_ROOT) / "Engine" / "Render";
    return ScanSourceDirectoryForForbiddenPatterns(
        renderRoot, {"water", "ocean", "sealevel", "submarinedepth", "buoyancy", "physicsworld",
                     "engine/physics"});
}

bool G2IntegrationAndPresentationArchitectureBoundaries()
{
    // G2 orchestration and presentation stay in Game. The generic renderer only accepts a node-local
    // transform override, while Engine/Physics remains unaware of propulsion and presentation concepts.
    const std::filesystem::path sourceRoot = DEEPRUN_SOURCE_ROOT;
    const std::filesystem::path renderRoot = sourceRoot / "Engine" / "Render";
    const std::filesystem::path physicsRoot = sourceRoot / "Engine" / "Physics";
    return PropulsionFilesHaveOnlyPureMarineDependencies() && GameCodeHasNoJoltDependency() &&
           ScanSourceDirectoryForForbiddenPatterns(
               renderRoot, {"propulsion", "shaft", "rpm", "propeller", "submarine"}) &&
           ScanSourceDirectoryForForbiddenPatterns(
               physicsRoot, {"propulsion", "shaft", "rpm", "propeller", "submarine"});
}

bool H2IntegrationArchitectureBoundaries()
{
    const std::filesystem::path sourceRoot = DEEPRUN_SOURCE_ROOT;
    const std::filesystem::path physicsRoot = sourceRoot / "Engine" / "Physics";
    const std::filesystem::path renderRoot = sourceRoot / "Engine" / "Render";
    return ControlSurfaceFilesHaveOnlyPureMarineDependencies() && GameCodeHasNoJoltDependency() &&
           ScanSourceDirectoryForForbiddenPatterns(
               physicsRoot, {"controlsurface", "control surface", "simulation/marine", "submarine"}) &&
           ScanSourceDirectoryForForbiddenPatterns(
               renderRoot, {"controlsurface", "control surface", "simulation/marine"});
}

bool I1SemanticInputArchitectureBoundaries()
{
    const std::filesystem::path sourceRoot = DEEPRUN_SOURCE_ROOT;
    const std::filesystem::path marineRoot = sourceRoot / "Simulation" / "Marine";
    const std::filesystem::path inputRoot = sourceRoot / "Engine" / "Input";
    const std::filesystem::path physicsRoot = sourceRoot / "Engine" / "Physics";
    const std::filesystem::path commandRoot = sourceRoot / "Game" / "Submarine";
    const std::filesystem::path playgroundRoot = sourceRoot / "Game";
    return ScanSourceDirectoryForForbiddenPatterns(
               marineRoot, {"engine/input", "inputaxis", "inputaction", "xinput", "game/"}) &&
           ScanSourceDirectoryForForbiddenPatterns(
               inputRoot, {"submarine", "simulation/marine", "physicsworld", "vesselcommand"}) &&
           ScanSourceDirectoryForForbiddenPatterns(
               physicsRoot, {"engine/input", "inputaxis", "vesselcommand", "vessel command"}) &&
           ScanSourceDirectoryForForbiddenPatterns(
               commandRoot, {"windows.h", "xinput", "platform::key", "vk_", "wm_key", "jph::", "<jolt/"}) &&
           ScanSourceDirectoryForForbiddenPatterns(
               playgroundRoot, {"platform::key", "xinput", "vk_", "wm_key", ".gamepad()", ".leftx", ".lefty"});
}

bool SourcePatternAppearsOnlyUnder(
    const std::filesystem::path& sourceRoot,
    const std::filesystem::path& allowedRoot,
    const std::string_view pattern)
{
    bool found = false;
    for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(sourceRoot))
    {
        if (!entry.is_regular_file() || (entry.path().extension() != ".h" && entry.path().extension() != ".cpp"))
        {
            continue;
        }
        std::ifstream input(entry.path(), std::ios::binary);
        std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        std::ranges::transform(contents, contents.begin(), [](const unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        if (contents.find(pattern) == std::string::npos)
        {
            continue;
        }
        found = true;
        const std::filesystem::path relative = entry.path().lexically_relative(allowedRoot);
        if (relative.empty() || *relative.begin() == "..")
        {
            return false;
        }
    }
    return found;
}

bool I2SemanticHapticArchitectureBoundaries()
{
    const std::filesystem::path sourceRoot = DEEPRUN_SOURCE_ROOT;
    const std::filesystem::path engineRoot = sourceRoot / "Engine";
    const std::filesystem::path inputRoot = engineRoot / "Input";
    const std::filesystem::path gameHapticsRoot = sourceRoot / "Game" / "Haptics";
    const std::filesystem::path marineRoot = sourceRoot / "Simulation" / "Marine";
    const std::filesystem::path commandRoot = sourceRoot / "Game" / "Submarine";
    const std::string backendCall = std::string("windows.gaming.") + "input";
    return ScanSourceDirectoryForForbiddenPatterns(
               engineRoot, {"enginevibration", "shaft rpm", "simulation/marine", "propeller"}) &&
           ScanSourceDirectoryForForbiddenPatterns(
               inputRoot, {"submarine", "marine", "enginevibration", "shaft", "rpm", "propeller"}) &&
           ScanSourceDirectoryForForbiddenPatterns(
               gameHapticsRoot, {"windows.h", "xinput", "xinput_vibration", "word motor", "setstate"}) &&
           ScanSourceDirectoryForForbiddenPatterns(marineRoot, {"haptic"}) &&
           ScanSourceDirectoryForForbiddenPatterns(commandRoot, {"haptic", "vibration", "motor"}) &&
           SourcePatternAppearsOnlyUnder(sourceRoot, inputRoot, backendCall);
}

bool M2MinimizedWindowSilencesHapticsBeforeWaiting()
{
    const std::filesystem::path enginePath =
        std::filesystem::path(DEEPRUN_SOURCE_ROOT) / "Engine" / "Core" / "Engine.cpp";
    std::ifstream input(enginePath, std::ios::binary);
    if (!input)
    {
        return false;
    }
    std::string source((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    std::ranges::transform(source, source.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });

    const std::size_t minimized = source.find("if (window->minimized())");
    const std::size_t silence = source.find("input->applygamepadvibration({})", minimized);
    const std::size_t wait = source.find("window->waitforevents()", minimized);
    const std::size_t rebase = source.find("timer.rebase()", minimized);
    const std::size_t nextTick = source.find("timer.tick()", minimized);
    return minimized != std::string::npos && silence != std::string::npos && wait != std::string::npos &&
           rebase != std::string::npos && nextTick != std::string::npos && minimized < silence &&
           silence < wait && wait < rebase && rebase < nextTick;
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
        {"E3 fixed-update hook runs before physics step", FixedUpdateHookRunsBeforePhysicsStep},
        {"E3 fixed-update hook failure prevents physics step", FixedUpdateHookFailurePreventsPhysicsStep},
        {"Headless engine lifecycle", EngineHeadlessLifecycle},
        {"Scene entity lifecycle", SceneEntityLifecycle},
        {"Resource identity and cache", ResourceIdentityAndCache},
        {"Asset path normalization", AssetPathNormalization},
        {"Canonical C0 model load", CanonicalModelLoads},
        {"IG1-A deterministic Antey runtime staging", IG1AStagesValidatedProductionPackageDeterministically},
        {"IG1-A staging rejects missing and legacy input", IG1AStagingRejectsMissingOrLegacyProductionInput},
        {"IG1-A staged Antey definition loads headlessly", IG1AStagedProductionDefinitionLoadsHeadlessly},
        {"IG1-A.1 authoring transform conversion preserves affine invariant", IG1A1AuthoringTransformConversionPreservesAffineInvariant},
        {"IG1-B selects staged production visual and validates runtime geometry", IG1BProductionVisualSelectionAndRuntimeGeometry},
        {"IG1-C removes the prototype physics bridge from normal runtime", IG1BNormalPlaygroundVisualIsIsolatedFromM2PhysicsBridge},
        {"IG1-C production proxy contracts are loaded independently from visual bounds", IG1CProductionProxyContractsAreLoadedAndIndependentFromVisualBounds},
        {"IG1-C production buoyancy layout stays bounded and Game-tuned", IG1CProductionBuoyancyLayoutIsBoundedAndGameTuned},
        {"IG1-B.1 stows only classified sail devices through opaque post transforms", IG1B1SubmergedSailDevicesUseOnlyOpaquePostTransforms},
        {"IG1-A.2 production semantic spatial metadata is geometry-derived", IG1A2ProductionSemanticSpatialMetadataIsGeometryDerived},
        {"IG1-A public semantic definition hides raw node names", IG1APublicSemanticDefinitionHasNoRawNodeNames},
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
        {"I1 semantic axis storage", I1SemanticAxisStorage},
        {"I1 keyboard semantic mapping", I1KeyboardSemanticMapping},
        {"I1 controller semantic mapping and dead zone", I1ControllerSemanticMapping},
        {"I1 controller disconnect and keyboard arbitration", I1ControllerDisconnectAndArbitration},
        {"WGI reading converts to generic gamepad state", WgiGamepadReadingConvertsToGenericState},
        {"WGI connection and focus neutralization", WgiConnectionAndFocusNeutralization},
        {"I1 vessel command semantic mapping and validation", I1VesselCommandMapsSemanticAxesAndRejectsMalformedInput},
        {"Deterministic random", DeterministicRandom},
        {"Jolt initialization", JoltInitialization},
        {"Rigid-body gravity", RigidBodySimulation},
        {"Physics gravity query contract", PhysicsGravityQueryContract},
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
        // M2 Slice E2: deterministic/headless multi-point buoyancy math (no rigid-body world).
        {"E2 invalid buoyancy configuration rejected", BuoyancyRejectsInvalidConfiguration},
        {"E2 invalid pose and gravity rejected", BuoyancyRejectsInvalidPoseAndGravity},
        {"E2 fully dry points preserved in order", BuoyancyFullyDryKeepsPointOrder},
        {"E2 fully submerged symmetric points", BuoyancyFullySubmergedSymmetricPoints},
        {"E2 exact surface is half submerged", BuoyancyExactSurfaceIsHalfSubmerged},
        {"E2 quarter and three-quarter submersion", BuoyancyQuarterAndThreeQuarterSubmersion},
        {"E2 shifted water surface regression", BuoyancyShiftedSurfaceRegression},
        {"E2 body-local orientation convention", BuoyancyBodyLocalOrientationConvention},
        {"E2 quaternion sign equivalence", BuoyancyQuaternionSignEquivalence},
        {"E2 pitch-sensitive bow and stern", BuoyancyPitchSensitiveBowAndStern},
        {"E2 neutral-displacement identity", BuoyancyNeutralDisplacementIdentity},
        {"E2 totals sum published point results", BuoyancyTotalsSumPublishedPointResults},
        {"E2 derived overflow rejected", BuoyancyRejectsDerivedOverflow},
        // M2 Slice E3: headless force production/application through public Marine + Physics APIs.
        {"E3 neutral buoyancy remains at rest", BuoyancyIntegrationNeutralRest},
        {"E3 neutral buoyancy preserves vertical velocity", BuoyancyIntegrationPreservesVerticalVelocity},
        {"E3 point forces create restoring pitch", BuoyancyPointForcesCreateRestoringPitch},
        // M2 Slice F1: pure fully-immersed directional drag force/torque calculation.
        {"F1 zero velocity returns exact zero", HydroDragZeroVelocityIsExactZero},
        {"F1 linear drag reverses sign", HydroDragLinearSignReversal},
        {"F1 linear drag scales quadratically", HydroDragLinearQuadraticSpeedScaling},
        {"F1 drag uses WaterBody density", HydroDragUsesWaterDensityScaling},
        {"F1 directional linear anisotropy", HydroDragDirectionalAnisotropy},
        {"F1 body-local linear orientation", HydroDragLinearBodyOrientation},
        {"F1 quaternion sign equivalence", HydroDragQuaternionSignEquivalence},
        {"F1 angular drag reverses sign", HydroDragAngularSignReversal},
        {"F1 angular drag scales quadratically", HydroDragAngularQuadraticSpeedScaling},
        {"F1 body-local angular orientation", HydroDragAngularBodyOrientation},
        {"F1 linear and angular independence", HydroDragLinearAngularIndependence},
        {"F1 zero coefficients are valid", HydroDragZeroCoefficientsAreValid},
        {"F1 invalid configuration rejected", HydroDragRejectsInvalidConfiguration},
        {"F1 invalid state rejected", HydroDragRejectsInvalidState},
        {"F1 derived overflow rejected", HydroDragRejectsDerivedOverflow},
        // M2 Slice H1: pure fully-immersed control-surface lift calculation.
        {"H1 zero deflection returns zero force", ControlSurfaceZeroDeflection},
        {"H1 zero speed has no authority", ControlSurfaceZeroSpeedHasNoAuthority},
        {"H1 ahead positive force formula", ControlSurfaceAheadPositiveForce},
        {"H1 deflection sign", ControlSurfaceDeflectionSign},
        {"H1 reverse flow reverses force", ControlSurfaceReverseFlowReversesForce},
        {"H1 quadratic speed scaling", ControlSurfaceQuadraticSpeedScaling},
        {"H1 linear deflection scaling", ControlSurfaceLinearDeflectionScaling},
        {"H1 WaterBody density scaling", ControlSurfaceUsesWaterDensity},
        {"H1 body orientation", ControlSurfaceBodyOrientation},
        {"H1 application point rotation", ControlSurfaceApplicationPointRotation},
        {"H1 quaternion sign equivalence", ControlSurfaceQuaternionSignEquivalence},
        {"H1 non-unit quaternion normalization", ControlSurfaceNormalizesNonUnitQuaternion},
        {"H1 zero effective area is valid", ControlSurfaceZeroAreaIsValid},
        {"H1 invalid configuration rejected", ControlSurfaceRejectsInvalidConfiguration},
        {"H1 invalid deflection rejected", ControlSurfaceRejectsInvalidDeflection},
        {"H1 invalid kinematics rejected", ControlSurfaceRejectsInvalidKinematics},
        {"H1 derived overflow rejected", ControlSurfaceRejectsDerivedOverflow},
        // M2 Slice F2: drag integration and generic transient torque application.
        {"F2 vertical drag damps descent without spring", HydroDragIntegrationDampsVerticalDescentWithoutSpring},
        {"F2 integrated linear anisotropy", HydroDragIntegrationPreservesLinearAnisotropy},
        {"F2 integrated body-local axes", HydroDragIntegrationUsesBodyLocalAxes},
        {"F2 integrated angular damping", HydroDragIntegrationDampsAngularVelocity},
        // M2 Slice G1: pure one-shaft RPM state evolution and signed scalar thrust.
        {"G1 idle is exact zero", PropulsionIdleIsExactZero},
        {"G1 zero power at rest is exact zero", PropulsionZeroPowerAtRestIsExactZero},
        {"G1 finite spin-up", PropulsionHasFiniteSpinUp},
        {"G1 configured RPM rate", PropulsionUsesConfiguredRpmRate},
        {"G1 reaches target without overshoot", PropulsionReachesTargetWithoutOvershoot},
        {"G1 step-size consistency", PropulsionIsStepSizeConsistent},
        {"G1 drive reduction spins down", PropulsionDriveReductionSpinsDown},
        {"G1 power loss coasts down", PropulsionPowerLossCoastsDown},
        {"G1 limited power sets reduced target", PropulsionLimitedPowerSetsReducedTarget},
        {"G1 reverse operation", PropulsionSupportsReverse},
        {"G1 direction change passes through zero", PropulsionDirectionChangePassesThroughZero},
        {"G1 quadratic RPM-to-thrust mapping", PropulsionThrustIsQuadraticInRpm},
        {"G1 asymmetric ahead/astern limits", PropulsionUsesAsymmetricAheadAsternLimits},
        {"G1 invalid configuration rejected", PropulsionRejectsInvalidConfiguration},
        {"G1 invalid command rejected", PropulsionRejectsInvalidCommand},
        {"G1 invalid state and delta rejected", PropulsionRejectsInvalidStateAndDeltaTime},
        {"G1 derived overflow rejected", PropulsionRejectsDerivedOverflow},
        // M2 Slice G2: public-API marine/physics integration and authoritative-RPM presentation.
        {"G2 body-local thrust and point transform", G2BodyLocalThrustAndPointTransform},
        {"G2 propeller presentation follows RPM", G2PropellerPresentationFollowsRpm},
        {"G2 propeller node contract validation", G2PropellerNodeContractValidation},
        {"G2 generic node override validation", G2NodeOverrideValidation},
        {"G2 node post-transform keeps hub and other nodes", G2NodePostTransformKeepsHubAndOtherNodes},
        {"G2 canonical draw and bounds gate", G2CanonicalDrawAndBoundsGate},
        {"G2 ahead acceleration and RPM ramp", G2AheadAccelerationAndRpmRamp},
        {"G2 natural terminal speed and hydrostatic stability",
         G2NaturalTerminalSpeedAndHydrostaticStability},
        {"G2 astern produces negative response", G2AsternProducesNegativeResponse},
        // M2 Slice H2: two published H1 forces integrated at distinct world points.
        {"H2 zero speed has no physical pitch response", H2ZeroSpeedHasNoPhysicalPitchResponse},
        {"H2 symmetric pair creates pitch without net lift",
         H2SymmetricPairCreatesNegativePitchWithoutNetLift},
        {"H2 opposite pair reverses pitch", H2OppositePairCreatesPositivePitch},
        {"H2 control authority grows with speed", H2ControlAuthorityGrowsWithSpeed},
        {"H2 propulsion pitch depth and control release", H2PropulsionPitchDepthAndControlRelease},
        {"I1 neutral vessel command is physical default", I1NeutralCommandIsPhysicalDefault},
        {"I1 throttle ahead release and astern", I1ThrottleAheadReleaseAndAstern},
        {"I1 depth command maps to physical dive and surface", I1DepthCommandMapsToPhysicalDiveAndSurface},
        {"I1 depth release and low speed have no magic authority", I1DepthReleaseAndLowSpeedHaveNoMagicAuthority},
        {"I1 command snapshot is fixed-step stable", I1CommandSnapshotIsFixedStepStable},
        // M2 Slice I2: semantic engine feedback, generic mixer, and normalized backend conversion.
        {"I2 mixer empty output is silent", I2MixerEmptyIsSilent},
        {"I2 mixer single effect", I2MixerSingleEffect},
        {"I2 mixer same-ID replace and refresh", I2MixerSameIdReplacesAndRefreshes},
        {"I2 mixer same-priority add and clamp", I2MixerSamePriorityAddsAndClamps},
        {"I2 mixer priority suppression and resume", I2MixerPrioritySuppressesAndResumes},
        {"I2 mixer deterministic duration expiry", I2MixerDurationExpiresDeterministically},
        {"I2 long current-frame submit survives first output", I2LongCurrentFrameSubmissionSurvivesFirstOutput},
        {"I2 old unrefreshed effect expires on long frame", I2OldUnrefreshedEffectExpiresOnLongFrame},
        {"I2 same-ID refresh after long-frame expiry does not stack",
         I2SameIdRefreshAfterLongFrameExpiryDoesNotStack},
        {"I2 normal-frame continuous refresh is stable", I2NormalFrameContinuousRefreshIsStable},
        {"I2 mixer master intensity", I2MixerMasterIntensity},
        {"I2 mixer disable keeps effects ageing", I2MixerDisableKeepsAgeing},
        {"I2 mixer rejects malformed configuration", I2MixerRejectsMalformedConfiguration},
        {"WGI normalized vibration conversion", WgiVibrationConversionUsesNormalizedMotors},
        {"I2 semantic engine-vibration mapping", I2SemanticEngineVibrationMapping},
        {"I2 authoritative directional RPM normalization", I2RpmNormalizationUsesAuthoritativeDirectionLimits},
        {"I2 RPM inertia produces haptic ramp", I2RpmRampProducesHapticRamp},
        {"I2 haptics cannot affect simulation", I2HapticsCannotAffectSimulation},
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
        {"M3-A Reinhard tone-map properties", M3AToneMapProperties},
        {"M3-I frame percentiles and invalid samples", M3IFramePercentiles},
        {"M3-A.1 display output selection", M3A1DisplayOutputSelection},
        {"M3-A.1 HDR scRGB mapping properties", M3A1HdrScRgbMappingProperties},
        {"M3-C underwater depth-lighting properties", M3CUnderwaterDepthLightingProperties},
        {"M3-C.1 view-path fog properties", M3C1ViewPathFogProperties},
        {"M3-D suspended underwater particle field properties", M3DUnderwaterParticleFieldProperties},
        {"M3-E Gerstner surface presentation properties", M3EGerstnerSurfacePresentationProperties},
        {"M3-E.1 wave definition validation", M3E1WaveDefinitionValidation},
        {"M3-E.1 world-X wave query and adapter parity", M3E1WaveQueryAndAdapterParity},
        {"M3-F representative surface-float model", M3FSurfaceFloatModelContract},
        {"M3-F explicit local wave buoyancy", M3FWaveBuoyancyIsExplicitAndLocal},
        {"M3-F representative float wave physics integration", M3FSurfaceFloatWavePhysicsIntegration},
        {"M3-G bounded underwater flora field", M3GBoundedUnderwaterFloraFieldContract},
        {"M3-H bounded underwater ice field", M3HBoundedUnderwaterIceFieldContract},
        {"M3-H ice render and collision representations are independent", M3HIceRenderAndCollisionRepresentationsAreIndependent},
        {"M3-H coarse ice collision stops dynamic box", M3HIceCoarseCollisionStopsDynamicBox},
        {"M3-H.1 bounded underwater fauna field", M3H1BoundedUnderwaterFaunaFieldContract},
        {"M3-H.1 fauna presentation motion", M3H1UnderwaterFaunaPresentationMotion},
        {"M3-B.2 representative authored terrain geometry", M3BSeabedSectionGeometryContract},
        {"M3-B.1 static body validation lifetime and contact", M3B1StaticBodyContract},
        {"M3-B.1 static bodies reject force and torque mutation", M3B1StaticBodyRejectsMutation},
        {"M3-B.1 independent collision topology", M3B1CollisionTopologyIndependent},
        {"M3-B.1 canonical submarine seabed contact", M3B1CanonicalSubmarineContact},
        {"M3-B.2 representative terrain collision contacts", M3B2RepresentativeTerrainCollisionContacts},
        {"M3-B seabed rejects malformed configuration and IDs", M3BSeabedSectionRejectsMalformedInput},
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
        {"E1 zero force does not wake sleeping body", ForceZeroDoesNotWakeSleepingBody},
        // M2 Slice F2: generic torque PhysicsWorld API (headless, public API only).
        {"F2 torque sign and orientation", TorqueSignAndOrientation},
        {"F2 torque multiple calls accumulate", TorqueMultipleCallsAccumulate},
        {"F2 torque is not persistent", TorqueIsNotPersistent},
        {"F2 torque validation and zero no-op", TorqueInvalidInputAndZeroNoOp},
        {"F2 non-zero torque activates sleeping body", TorqueActivatesSleepingBody},
        {"F2 zero torque does not wake sleeping body", TorqueZeroDoesNotWakeSleepingBody},
        {"F2 planar DOF preservation under torque", TorquePlanarDOFPreservation},
        // M2 Slice C2: architecture boundary scans.
        {"Game code has no Jolt dependency", GameCodeHasNoJoltDependency},
        {"Engine render has no physics or Jolt dependency", EngineRenderHasNoPhysicsOrJoltDependency},
        {"Physics world has no GPU model knowledge", PhysicsWorldHasNoGpuModelKnowledge},
        {"Physics public headers have no Jolt dependency", PhysicsPublicHeadersHaveNoJoltDependency},
        // M2 Slice D1: architecture boundary scans.
        {"Simulation marine has no physics or render dependency",
         SimulationMarineHasNoPhysicsOrRenderDependency},
        {"Simulation has no Gerstner render-presentation dependency",
         SimulationHasNoGerstnerRenderPresentationDependency},
        {"M3-F wave buoyancy has no render dependency", M3FWaveBuoyancyHasNoRenderDependency},
        {"M3-G flora stays presentation-only", M3GFloraStaysPresentationOnly},
        {"M3-H ice stays Game-owned and static", M3HIceStaysGameOwnedAndStatic},
        {"M3-H.1 fauna stays presentation-only", M3H1FaunaStaysPresentationOnly},
        {"F1 HydroDrag files have only pure Marine dependencies", HydroDragFilesHaveOnlyPureMarineDependencies},
        {"G1 Propulsion files have only pure Marine dependencies",
         PropulsionFilesHaveOnlyPureMarineDependencies},
        {"H1 ControlSurface files have only pure Marine dependencies",
         ControlSurfaceFilesHaveOnlyPureMarineDependencies},
        {"Engine has no marine knowledge", EngineHasNoMarineKnowledge},
        // M2 Slice D2: renderer stays generic — no water semantics in Engine/Render.
        {"Engine render has no water semantics", EngineRenderHasNoWaterSemantics},
        {"G2 integration and presentation architecture boundaries",
         G2IntegrationAndPresentationArchitectureBoundaries},
        {"H2 integration architecture boundaries", H2IntegrationArchitectureBoundaries},
        {"I1 semantic input architecture boundaries", I1SemanticInputArchitectureBoundaries},
        {"I2 semantic haptic architecture boundaries", I2SemanticHapticArchitectureBoundaries},
        {"M2 minimized window silences haptics before waiting", M2MinimizedWindowSilencesHapticsBeforeWaiting},
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
