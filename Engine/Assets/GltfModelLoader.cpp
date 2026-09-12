#include "Engine/Assets/GltfModelLoader.h"

#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace DeepRun::Assets
{
namespace
{
using ValidationResult = std::expected<void, AssetError>;

AssetError MakeError(
    const AssetErrorCode code,
    const std::filesystem::path& path,
    const AssetId& id,
    const std::string_view detail)
{
    std::string message = "model asset '";
    message.append(id.Value());
    message.append("': ");
    message.append(detail);
    return AssetError{code, path, std::move(message)};
}

std::string PrimitiveContext(
    const fastgltf::Mesh& mesh,
    const std::size_t meshIndex,
    const std::size_t primitiveIndex)
{
    std::ostringstream context;
    context << "mesh ";
    if (!mesh.name.empty())
    {
        context << '\'' << mesh.name << "' ";
    }
    context << '[' << meshIndex << "] primitive [" << primitiveIndex << ']';
    return context.str();
}

bool RangeFits(const std::size_t offset, const std::size_t length, const std::size_t available) noexcept
{
    return offset <= available && length <= available - offset;
}

std::optional<std::size_t> LoadedBufferSize(const fastgltf::Buffer& buffer)
{
    return std::visit(
        [](const auto& source) -> std::optional<std::size_t>
        {
            using Source = std::decay_t<decltype(source)>;
            if constexpr (std::is_same_v<Source, fastgltf::sources::Array> ||
                          std::is_same_v<Source, fastgltf::sources::Vector>)
            {
                return source.bytes.size();
            }
            else if constexpr (std::is_same_v<Source, fastgltf::sources::ByteView>)
            {
                return source.bytes.size();
            }
            else
            {
                return std::nullopt;
            }
        },
        buffer.data);
}

ValidationResult ValidateAccessorStorage(
    const fastgltf::Asset& asset,
    const std::size_t accessorIndex,
    const std::filesystem::path& path,
    const AssetId& id,
    const std::string_view context)
{
    if (accessorIndex >= asset.accessors.size())
    {
        return std::unexpected(MakeError(
            AssetErrorCode::InvalidData, path, id, std::string(context) + ": accessor index is out of range"));
    }

    const fastgltf::Accessor& accessor = asset.accessors[accessorIndex];
    if (accessor.sparse.has_value())
    {
        return std::unexpected(MakeError(
            AssetErrorCode::UnsupportedData, path, id, std::string(context) + ": sparse accessors are not supported"));
    }
    if (!accessor.bufferViewIndex.has_value())
    {
        return std::unexpected(MakeError(
            AssetErrorCode::InvalidData, path, id, std::string(context) + ": accessor has no buffer view"));
    }
    if (*accessor.bufferViewIndex >= asset.bufferViews.size())
    {
        return std::unexpected(MakeError(
            AssetErrorCode::InvalidData, path, id, std::string(context) + ": buffer-view index is out of range"));
    }

    const fastgltf::BufferView& view = asset.bufferViews[*accessor.bufferViewIndex];
    if (view.bufferIndex >= asset.buffers.size())
    {
        return std::unexpected(MakeError(
            AssetErrorCode::InvalidData, path, id, std::string(context) + ": buffer index is out of range"));
    }

    const fastgltf::Buffer& buffer = asset.buffers[view.bufferIndex];
    const std::optional<std::size_t> loadedSize = LoadedBufferSize(buffer);
    if (!loadedSize.has_value())
    {
        return std::unexpected(MakeError(
            AssetErrorCode::UnsupportedData,
            path,
            id,
            std::string(context) + ": accessor data is not embedded in the GLB"));
    }
    if (!RangeFits(view.byteOffset, view.byteLength, buffer.byteLength) ||
        !RangeFits(view.byteOffset, view.byteLength, *loadedSize))
    {
        return std::unexpected(MakeError(
            AssetErrorCode::InvalidData, path, id, std::string(context) + ": buffer view exceeds its buffer"));
    }

    const std::size_t elementSize = fastgltf::getElementByteSize(accessor.type, accessor.componentType);
    const std::size_t stride = view.byteStride.value_or(elementSize);
    if (accessor.count == 0 || elementSize == 0 || stride < elementSize)
    {
        return std::unexpected(MakeError(
            AssetErrorCode::InvalidData, path, id, std::string(context) + ": accessor layout is invalid"));
    }
    if (accessor.count - 1 > (std::numeric_limits<std::size_t>::max() - elementSize) / stride)
    {
        return std::unexpected(MakeError(
            AssetErrorCode::InvalidData, path, id, std::string(context) + ": accessor byte range overflows"));
    }

    const std::size_t requiredBytes = (accessor.count - 1) * stride + elementSize;
    if (!RangeFits(accessor.byteOffset, requiredBytes, view.byteLength))
    {
        return std::unexpected(MakeError(
            AssetErrorCode::InvalidData, path, id, std::string(context) + ": accessor exceeds its buffer view"));
    }
    return {};
}

ValidationResult ValidateSupportedAsset(
    const fastgltf::Asset& asset,
    const std::filesystem::path& path,
    const AssetId& id)
{
    if (!asset.extensionsUsed.empty() || !asset.extensionsRequired.empty())
    {
        return std::unexpected(MakeError(
            AssetErrorCode::UnsupportedData, path, id, "glTF extensions are outside the C0 model subset"));
    }
    if (!asset.skins.empty())
    {
        return std::unexpected(MakeError(
            AssetErrorCode::UnsupportedData, path, id, "skins remain outside the bounded C0.1 model subset"));
    }

    // C0.1 deliberately admits only immutable metadata for LINEAR rotation-only clips. The Engine still does
    // not evaluate arbitrary glTF animation, and translation/scale/weights/cubic/step channels remain rejected.
    for (std::size_t animationIndex = 0; animationIndex < asset.animations.size(); ++animationIndex)
    {
        const fastgltf::Animation& animation = asset.animations[animationIndex];
        const std::string context = "animation [" + std::to_string(animationIndex) + "]";
        if (animation.name.empty() || animation.samplers.empty() || animation.channels.empty())
        {
            return std::unexpected(MakeError(
                AssetErrorCode::InvalidData, path, id, context + ": named non-empty clip is required"));
        }

        for (std::size_t samplerIndex = 0; samplerIndex < animation.samplers.size(); ++samplerIndex)
        {
            const fastgltf::AnimationSampler& sampler = animation.samplers[samplerIndex];
            const std::string samplerContext = context + " sampler [" + std::to_string(samplerIndex) + "]";
            if (sampler.inputAccessor >= asset.accessors.size() || sampler.outputAccessor >= asset.accessors.size())
            {
                return std::unexpected(MakeError(
                    AssetErrorCode::InvalidData, path, id, samplerContext + ": accessor index is out of range"));
            }
            if (sampler.interpolation != fastgltf::AnimationInterpolation::Linear)
            {
                return std::unexpected(MakeError(
                    AssetErrorCode::UnsupportedData, path, id, samplerContext + ": only LINEAR interpolation is supported"));
            }
            if (auto result = ValidateAccessorStorage(
                    asset, sampler.inputAccessor, path, id, samplerContext + " input");
                !result)
            {
                return result;
            }
            if (auto result = ValidateAccessorStorage(
                    asset, sampler.outputAccessor, path, id, samplerContext + " output");
                !result)
            {
                return result;
            }

            const fastgltf::Accessor& input = asset.accessors[sampler.inputAccessor];
            const fastgltf::Accessor& output = asset.accessors[sampler.outputAccessor];
            if (input.type != fastgltf::AccessorType::Scalar || input.componentType != fastgltf::ComponentType::Float ||
                output.type != fastgltf::AccessorType::Vec4 || output.componentType != fastgltf::ComponentType::Float ||
                input.count < 2U || output.count != input.count)
            {
                return std::unexpected(MakeError(
                    AssetErrorCode::UnsupportedData,
                    path,
                    id,
                    samplerContext + ": rotation clips require float SCALAR times and matching float VEC4 quaternions"));
            }

            bool validTimes = true;
            float previousTime = -std::numeric_limits<float>::infinity();
            fastgltf::iterateAccessor<float>(asset, input, [&validTimes, &previousTime](const float value)
            {
                validTimes = validTimes && std::isfinite(value) && value >= 0.0F && value >= previousTime;
                previousTime = value;
            });
            bool validRotations = true;
            fastgltf::iterateAccessor<fastgltf::math::fvec4>(asset, output, [&validRotations](const auto value)
            {
                const double lengthSquared =
                    static_cast<double>(value.x()) * value.x() + static_cast<double>(value.y()) * value.y() +
                    static_cast<double>(value.z()) * value.z() + static_cast<double>(value.w()) * value.w();
                validRotations = validRotations && std::isfinite(value.x()) && std::isfinite(value.y()) &&
                    std::isfinite(value.z()) && std::isfinite(value.w()) && std::isfinite(lengthSquared) &&
                    lengthSquared > 1.0e-8;
            });
            if (!validTimes || !validRotations || previousTime <= 0.0F)
            {
                return std::unexpected(MakeError(
                    AssetErrorCode::InvalidData, path, id, samplerContext + ": keyframe data is invalid"));
            }
        }

        for (std::size_t channelIndex = 0; channelIndex < animation.channels.size(); ++channelIndex)
        {
            const fastgltf::AnimationChannel& channel = animation.channels[channelIndex];
            const std::string channelContext = context + " channel [" + std::to_string(channelIndex) + "]";
            if (channel.samplerIndex >= animation.samplers.size() || !channel.nodeIndex.has_value() ||
                *channel.nodeIndex >= asset.nodes.size())
            {
                return std::unexpected(MakeError(
                    AssetErrorCode::InvalidData, path, id, channelContext + ": sampler/node index is invalid"));
            }
            if (channel.path != fastgltf::AnimationPath::Rotation)
            {
                return std::unexpected(MakeError(
                    AssetErrorCode::UnsupportedData, path, id, channelContext + ": only rotation channels are supported"));
            }
            if (asset.nodes[*channel.nodeIndex].name.empty())
            {
                return std::unexpected(MakeError(
                    AssetErrorCode::InvalidData, path, id, channelContext + ": target node must be named"));
            }
        }
    }
    if (asset.scenes.empty())
    {
        return std::unexpected(MakeError(AssetErrorCode::InvalidData, path, id, "GLB contains no scene"));
    }
    if (asset.defaultScene.has_value() && *asset.defaultScene >= asset.scenes.size())
    {
        return std::unexpected(MakeError(
            AssetErrorCode::InvalidData, path, id, "default scene index is out of range"));
    }

    for (std::size_t viewIndex = 0; viewIndex < asset.bufferViews.size(); ++viewIndex)
    {
        const fastgltf::BufferView& view = asset.bufferViews[viewIndex];
        if (view.meshoptCompression != nullptr)
        {
            return std::unexpected(MakeError(
                AssetErrorCode::UnsupportedData, path, id, "meshopt compression is not supported"));
        }
        if (view.bufferIndex >= asset.buffers.size())
        {
            return std::unexpected(MakeError(
                AssetErrorCode::InvalidData, path, id, "buffer view references an invalid buffer"));
        }
    }

    for (std::size_t meshIndex = 0; meshIndex < asset.meshes.size(); ++meshIndex)
    {
        const fastgltf::Mesh& mesh = asset.meshes[meshIndex];
        for (std::size_t primitiveIndex = 0; primitiveIndex < mesh.primitives.size(); ++primitiveIndex)
        {
            const fastgltf::Primitive& primitive = mesh.primitives[primitiveIndex];
            const std::string context = PrimitiveContext(mesh, meshIndex, primitiveIndex);
            if (primitive.dracoCompression != nullptr)
            {
                return std::unexpected(MakeError(
                    AssetErrorCode::UnsupportedData, path, id, context + ": Draco compression is not supported"));
            }
            if (!primitive.targets.empty())
            {
                return std::unexpected(MakeError(
                    AssetErrorCode::UnsupportedData, path, id, context + ": morph targets are not supported"));
            }
            if (primitive.materialIndex.has_value() && *primitive.materialIndex >= asset.materials.size())
            {
                return std::unexpected(MakeError(
                    AssetErrorCode::InvalidData, path, id, context + ": material index is out of range"));
            }
            for (const fastgltf::Attribute& attribute : primitive.attributes)
            {
                std::string attributeContext = context + " attribute '";
                attributeContext.append(attribute.name.data(), attribute.name.size());
                attributeContext.push_back('\'');
                if (auto result = ValidateAccessorStorage(
                        asset, attribute.accessorIndex, path, id, attributeContext);
                    !result)
                {
                    return result;
                }
            }
            if (primitive.indicesAccessor.has_value())
            {
                if (auto result = ValidateAccessorStorage(
                        asset, *primitive.indicesAccessor, path, id, context + " indices");
                    !result)
                {
                    return result;
                }
            }
        }
    }

    for (const fastgltf::Node& node : asset.nodes)
    {
        if (node.skinIndex.has_value())
        {
            return std::unexpected(MakeError(
                AssetErrorCode::UnsupportedData, path, id, "skinned mesh nodes are not supported"));
        }
        if (node.meshIndex.has_value() && *node.meshIndex >= asset.meshes.size())
        {
            return std::unexpected(MakeError(
                AssetErrorCode::InvalidData, path, id, "node mesh index is out of range"));
        }
        if (std::any_of(node.children.begin(), node.children.end(), [&asset](const std::size_t child)
            {
                return child >= asset.nodes.size();
            }))
        {
            return std::unexpected(MakeError(
                AssetErrorCode::InvalidData, path, id, "node child index is out of range"));
        }
    }

    const std::size_t sceneIndex = asset.defaultScene.value_or(0);
    const fastgltf::Scene& scene = asset.scenes[sceneIndex];
    if (scene.nodeIndices.empty())
    {
        return std::unexpected(MakeError(AssetErrorCode::InvalidData, path, id, "active scene contains no nodes"));
    }
    if (std::any_of(scene.nodeIndices.begin(), scene.nodeIndices.end(), [&asset](const std::size_t nodeIndex)
        {
            return nodeIndex >= asset.nodes.size();
        }))
    {
        return std::unexpected(MakeError(
            AssetErrorCode::InvalidData, path, id, "active scene root index is out of range"));
    }
    return {};
}

bool IsFinite(const ModelVector3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool IsFinite(const ModelMaterialData& material) noexcept
{
    return std::all_of(
               material.baseColorFactor.begin(),
               material.baseColorFactor.end(),
               [](const float component) { return std::isfinite(component); }) &&
           std::isfinite(material.metallicFactor) && std::isfinite(material.roughnessFactor);
}

void ExpandBounds(ModelBounds& bounds, bool& initialized, const ModelVector3& point) noexcept
{
    if (!initialized)
    {
        bounds.minimum = point;
        bounds.maximum = point;
        initialized = true;
        return;
    }

    bounds.minimum.x = std::min(bounds.minimum.x, point.x);
    bounds.minimum.y = std::min(bounds.minimum.y, point.y);
    bounds.minimum.z = std::min(bounds.minimum.z, point.z);
    bounds.maximum.x = std::max(bounds.maximum.x, point.x);
    bounds.maximum.y = std::max(bounds.maximum.y, point.y);
    bounds.maximum.z = std::max(bounds.maximum.z, point.z);
}

bool IsFiniteAffine(const fastgltf::math::fmat4x4& matrix) noexcept
{
    for (std::size_t column = 0; column < 4; ++column)
    {
        for (std::size_t row = 0; row < 4; ++row)
        {
            if (!std::isfinite(matrix[column][row]))
            {
                return false;
            }
        }
    }

    constexpr float Epsilon = 1.0e-5F;
    return std::abs(matrix[0][3]) <= Epsilon && std::abs(matrix[1][3]) <= Epsilon &&
           std::abs(matrix[2][3]) <= Epsilon && std::abs(matrix[3][3] - 1.0F) <= Epsilon;
}

ModelTransform ToModelTransform(const fastgltf::math::fmat4x4& matrix) noexcept
{
    ModelTransform transform;
    for (std::size_t column = 0; column < 4; ++column)
    {
        for (std::size_t row = 0; row < 4; ++row)
        {
            transform.values[column * 4 + row] = matrix[column][row];
        }
    }
    return transform;
}

ModelVector3 TransformPoint(
    const fastgltf::math::fmat4x4& transform,
    const ModelVector3& point) noexcept
{
    const fastgltf::math::fvec4 transformed =
        transform * fastgltf::math::fvec4(point.x, point.y, point.z, 1.0F);
    return {transformed.x(), transformed.y(), transformed.z()};
}

class ModelImporter final
{
public:
    ModelImporter(
        const fastgltf::Asset& source,
        const AssetId& id,
        const std::filesystem::path& path)
        : source_(source), id_(id), path_(path), output_{id, {}, {}, {}, {}, {}, {}},
          meshPrimitiveCache_(source.meshes.size()), nodeVisitState_(source.nodes.size(), 0)
    {
    }

    std::expected<ModelAsset, AssetError> Import()
    {
        if (auto result = ImportMaterials(); !result)
        {
            return std::unexpected(std::move(result.error()));
        }

        const std::size_t sceneIndex = source_.defaultScene.value_or(0);
        for (const std::size_t nodeIndex : source_.scenes[sceneIndex].nodeIndices)
        {
            if (auto result = VisitNode(nodeIndex, fastgltf::math::fmat4x4()); !result)
            {
                return std::unexpected(std::move(result.error()));
            }
        }
        if (auto result = ImportAnimations(); !result)
        {
            return std::unexpected(std::move(result.error()));
        }

        if (output_.nodes.empty() || output_.primitives.empty() || !modelBoundsInitialized_)
        {
            return std::unexpected(MakeError(
                AssetErrorCode::InvalidData, path_, id_, "active scene contains no renderable indexed geometry"));
        }
        return std::move(output_);
    }

private:
    ValidationResult ImportAnimations()
    {
        output_.animations.reserve(source_.animations.size());
        for (std::size_t animationIndex = 0; animationIndex < source_.animations.size(); ++animationIndex)
        {
            const fastgltf::Animation& sourceAnimation = source_.animations[animationIndex];
            ModelAnimationClipData clip;
            clip.name = std::string(sourceAnimation.name);

            for (const fastgltf::AnimationChannel& channel : sourceAnimation.channels)
            {
                if (!channel.nodeIndex.has_value() || *channel.nodeIndex >= nodeVisitState_.size() ||
                    nodeVisitState_[*channel.nodeIndex] != 2U)
                {
                    return std::unexpected(MakeError(
                        AssetErrorCode::InvalidData,
                        path_,
                        id_,
                        "animation '" + clip.name + "' targets a node outside the active imported scene"));
                }
                const std::string targetName(source_.nodes[*channel.nodeIndex].name);
                if (std::find(clip.targetNodeNames.begin(), clip.targetNodeNames.end(), targetName) == clip.targetNodeNames.end())
                {
                    clip.targetNodeNames.push_back(targetName);
                }

                const fastgltf::AnimationSampler& sampler = sourceAnimation.samplers[channel.samplerIndex];
                const fastgltf::Accessor& input = source_.accessors[sampler.inputAccessor];
                fastgltf::iterateAccessor<float>(source_, input, [&clip](const float timeSeconds)
                {
                    clip.durationSeconds = std::max(clip.durationSeconds, timeSeconds);
                });
            }
            if (clip.targetNodeNames.empty() || !std::isfinite(clip.durationSeconds) || clip.durationSeconds <= 0.0F)
            {
                return std::unexpected(MakeError(
                    AssetErrorCode::InvalidData, path_, id_, "animation '" + clip.name + "' has no valid targets/duration"));
            }
            output_.animations.push_back(std::move(clip));
        }
        return {};
    }

    ValidationResult ImportMaterials()
    {
        output_.materials.reserve(source_.materials.size());
        for (std::size_t materialIndex = 0; materialIndex < source_.materials.size(); ++materialIndex)
        {
            const fastgltf::Material& sourceMaterial = source_.materials[materialIndex];
            const fastgltf::PBRData& pbr = sourceMaterial.pbrData;

            ModelMaterialData material;
            material.name = std::string(sourceMaterial.name);
            material.baseColorFactor = {
                static_cast<float>(pbr.baseColorFactor.x()),
                static_cast<float>(pbr.baseColorFactor.y()),
                static_cast<float>(pbr.baseColorFactor.z()),
                static_cast<float>(pbr.baseColorFactor.w())};
            material.metallicFactor = static_cast<float>(pbr.metallicFactor);
            material.roughnessFactor = static_cast<float>(pbr.roughnessFactor);
            if (!IsFinite(material))
            {
                return std::unexpected(MakeError(
                    AssetErrorCode::InvalidData,
                    path_,
                    id_,
                    "material [" + std::to_string(materialIndex) + "] contains non-finite PBR factors"));
            }
            output_.materials.push_back(std::move(material));
        }
        return {};
    }

    std::expected<std::vector<std::size_t>, AssetError> ImportMesh(const std::size_t meshIndex)
    {
        if (meshPrimitiveCache_[meshIndex].has_value())
        {
            return *meshPrimitiveCache_[meshIndex];
        }

        const fastgltf::Mesh& mesh = source_.meshes[meshIndex];
        if (mesh.primitives.empty())
        {
            return std::unexpected(MakeError(
                AssetErrorCode::InvalidData, path_, id_, "mesh contains no primitives"));
        }

        std::vector<std::size_t> importedIndices;
        importedIndices.reserve(mesh.primitives.size());
        for (std::size_t primitiveIndex = 0; primitiveIndex < mesh.primitives.size(); ++primitiveIndex)
        {
            auto primitiveResult = ImportPrimitive(mesh, meshIndex, primitiveIndex);
            if (!primitiveResult)
            {
                return std::unexpected(std::move(primitiveResult.error()));
            }
            importedIndices.push_back(output_.primitives.size());
            output_.primitives.push_back(std::move(*primitiveResult));
        }

        meshPrimitiveCache_[meshIndex] = importedIndices;
        return importedIndices;
    }

    std::expected<MeshPrimitiveData, AssetError> ImportPrimitive(
        const fastgltf::Mesh& mesh,
        const std::size_t meshIndex,
        const std::size_t primitiveIndex)
    {
        const fastgltf::Primitive& sourcePrimitive = mesh.primitives[primitiveIndex];
        const std::string context = PrimitiveContext(mesh, meshIndex, primitiveIndex);
        if (sourcePrimitive.type != fastgltf::PrimitiveType::Triangles)
        {
            return std::unexpected(MakeError(
                AssetErrorCode::UnsupportedData, path_, id_, context + ": only TRIANGLES topology is supported"));
        }

        const auto positionAttribute = sourcePrimitive.findAttribute("POSITION");
        if (positionAttribute == sourcePrimitive.attributes.end())
        {
            return std::unexpected(MakeError(
                AssetErrorCode::InvalidData, path_, id_, context + ": POSITION attribute is required"));
        }

        const fastgltf::Accessor& positionAccessor = source_.accessors[positionAttribute->accessorIndex];
        if (positionAccessor.type != fastgltf::AccessorType::Vec3 ||
            positionAccessor.componentType != fastgltf::ComponentType::Float)
        {
            return std::unexpected(MakeError(
                AssetErrorCode::UnsupportedData,
                path_,
                id_,
                context + ": POSITION must use float VEC3 data"));
        }
        if (positionAccessor.count > std::numeric_limits<std::uint32_t>::max())
        {
            return std::unexpected(MakeError(
                AssetErrorCode::UnsupportedData, path_, id_, context + ": vertex count exceeds 32-bit indices"));
        }

        MeshPrimitiveData primitive;
        primitive.materialIndex = sourcePrimitive.materialIndex;
        primitive.vertices.resize(positionAccessor.count);
        bool localBoundsInitialized = false;
        bool finitePositions = true;
        fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(
            source_,
            positionAccessor,
            [&primitive, &localBoundsInitialized, &finitePositions](
                const fastgltf::math::fvec3 position,
                const std::size_t index)
            {
                const ModelVector3 value{position.x(), position.y(), position.z()};
                finitePositions = finitePositions && IsFinite(value);
                primitive.vertices[index].position = value;
                ExpandBounds(primitive.localBounds, localBoundsInitialized, value);
            });
        if (!finitePositions || !localBoundsInitialized)
        {
            return std::unexpected(MakeError(
                AssetErrorCode::InvalidData, path_, id_, context + ": POSITION contains non-finite or empty data"));
        }

        if (const auto normalAttribute = sourcePrimitive.findAttribute("NORMAL");
            normalAttribute != sourcePrimitive.attributes.end())
        {
            const fastgltf::Accessor& normalAccessor = source_.accessors[normalAttribute->accessorIndex];
            if (normalAccessor.type != fastgltf::AccessorType::Vec3 ||
                normalAccessor.componentType != fastgltf::ComponentType::Float)
            {
                return std::unexpected(MakeError(
                    AssetErrorCode::UnsupportedData,
                    path_,
                    id_,
                    context + ": NORMAL must use float VEC3 data"));
            }
            if (normalAccessor.count != positionAccessor.count)
            {
                return std::unexpected(MakeError(
                    AssetErrorCode::InvalidData,
                    path_,
                    id_,
                    context + ": NORMAL count does not match POSITION count"));
            }

            bool finiteNormals = true;
            fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(
                source_,
                normalAccessor,
                [&primitive, &finiteNormals](const fastgltf::math::fvec3 normal, const std::size_t index)
                {
                    const ModelVector3 value{normal.x(), normal.y(), normal.z()};
                    finiteNormals = finiteNormals && IsFinite(value);
                    primitive.vertices[index].normal = value;
                });
            if (!finiteNormals)
            {
                return std::unexpected(MakeError(
                    AssetErrorCode::InvalidData, path_, id_, context + ": NORMAL contains non-finite data"));
            }
            primitive.hasNormals = true;
        }

        if (!sourcePrimitive.indicesAccessor.has_value())
        {
            return std::unexpected(MakeError(
                AssetErrorCode::InvalidData, path_, id_, context + ": indexed geometry is required"));
        }

        const fastgltf::Accessor& indexAccessor = source_.accessors[*sourcePrimitive.indicesAccessor];
        const bool supportedIndexComponent =
            indexAccessor.componentType == fastgltf::ComponentType::UnsignedByte ||
            indexAccessor.componentType == fastgltf::ComponentType::UnsignedShort ||
            indexAccessor.componentType == fastgltf::ComponentType::UnsignedInt;
        if (indexAccessor.type != fastgltf::AccessorType::Scalar || !supportedIndexComponent)
        {
            return std::unexpected(MakeError(
                AssetErrorCode::UnsupportedData,
                path_,
                id_,
                context + ": indices must use unsigned scalar data"));
        }
        if (indexAccessor.count < 3 || indexAccessor.count % 3 != 0)
        {
            return std::unexpected(MakeError(
                AssetErrorCode::InvalidData, path_, id_, context + ": triangle index count is invalid"));
        }

        primitive.indices.reserve(indexAccessor.count);
        bool indicesInRange = true;
        fastgltf::iterateAccessor<std::uint32_t>(
            source_,
            indexAccessor,
            [&primitive, &indicesInRange](const std::uint32_t index)
            {
                indicesInRange = indicesInRange && index < primitive.vertices.size();
                primitive.indices.push_back(index);
            });
        if (!indicesInRange)
        {
            return std::unexpected(MakeError(
                AssetErrorCode::InvalidData, path_, id_, context + ": index references a vertex out of range"));
        }
        return primitive;
    }

    ValidationResult VisitNode(
        const std::size_t nodeIndex,
        const fastgltf::math::fmat4x4& parentTransform)
    {
        if (nodeVisitState_[nodeIndex] != 0)
        {
            return std::unexpected(MakeError(
                AssetErrorCode::InvalidData, path_, id_, "scene graph contains a cycle or repeated node"));
        }
        nodeVisitState_[nodeIndex] = 1;

        const fastgltf::Node& sourceNode = source_.nodes[nodeIndex];
        const fastgltf::math::fmat4x4 localToModel = fastgltf::getTransformMatrix(sourceNode, parentTransform);
        if (!IsFiniteAffine(localToModel))
        {
            return std::unexpected(MakeError(
                AssetErrorCode::InvalidData, path_, id_, "mesh node transform is non-finite or non-affine"));
        }

        ModelNodeBindingData binding{
            .name = std::string(sourceNode.name), .localToModel = ToModelTransform(localToModel)};

        if (sourceNode.meshIndex.has_value())
        {
            binding.meshNodeIndex = output_.nodes.size();
            binding.drawableMeshNodeIndices.push_back(*binding.meshNodeIndex);
            auto primitiveIndices = ImportMesh(*sourceNode.meshIndex);
            if (!primitiveIndices)
            {
                return std::unexpected(std::move(primitiveIndices.error()));
            }

            MeshNodeData node;
            node.name = std::string(sourceNode.name);
            node.localToModel = ToModelTransform(localToModel);
            node.primitiveIndices = std::move(*primitiveIndices);
            for (const std::size_t primitiveIndex : node.primitiveIndices)
            {
                for (const MeshVertex& vertex : output_.primitives[primitiveIndex].vertices)
                {
                    const ModelVector3 transformed = TransformPoint(localToModel, vertex.position);
                    if (!IsFinite(transformed))
                    {
                        return std::unexpected(MakeError(
                            AssetErrorCode::InvalidData,
                            path_,
                            id_,
                            "node transform produced non-finite model bounds"));
                    }
                    ExpandBounds(output_.bounds, modelBoundsInitialized_, transformed);
                }
            }
            output_.nodes.push_back(std::move(node));
        }
        const std::size_t bindingIndex = output_.nodeBindings.size();
        output_.nodeBindings.push_back(std::move(binding));

        for (const std::size_t childIndex : sourceNode.children)
        {
            const std::size_t childBindingIndex = output_.nodeBindings.size();
            if (auto result = VisitNode(childIndex, localToModel); !result)
            {
                return result;
            }
            if (childBindingIndex >= output_.nodeBindings.size())
            {
                return std::unexpected(MakeError(
                    AssetErrorCode::InvalidData, path_, id_, "scene child produced no imported node binding"));
            }
            const auto& childDrawables = output_.nodeBindings[childBindingIndex].drawableMeshNodeIndices;
            auto& parentDrawables = output_.nodeBindings[bindingIndex].drawableMeshNodeIndices;
            parentDrawables.insert(parentDrawables.end(), childDrawables.begin(), childDrawables.end());
        }
        nodeVisitState_[nodeIndex] = 2;
        return {};
    }

    const fastgltf::Asset& source_;
    const AssetId& id_;
    const std::filesystem::path& path_;
    ModelAsset output_;
    std::vector<std::optional<std::vector<std::size_t>>> meshPrimitiveCache_;
    std::vector<std::uint8_t> nodeVisitState_;
    bool modelBoundsInitialized_ = false;
};
}

std::expected<ModelAsset, AssetError> LoadGltfModel(
    const AssetId& id,
    const std::filesystem::path& resolvedPath)
{
    if (resolvedPath.extension() != ".glb")
    {
        return std::unexpected(MakeError(
            AssetErrorCode::UnsupportedData, resolvedPath, id, "only binary glTF (.glb) models are supported"));
    }

    std::error_code fileError;
    const bool exists = std::filesystem::exists(resolvedPath, fileError);
    if (fileError)
    {
        return std::unexpected(MakeError(
            AssetErrorCode::ReadFailed, resolvedPath, id, "failed to inspect model path"));
    }
    if (!exists)
    {
        return std::unexpected(MakeError(AssetErrorCode::NotFound, resolvedPath, id, "model file was not found"));
    }

    auto data = fastgltf::GltfDataBuffer::FromPath(resolvedPath);
    if (data.error() != fastgltf::Error::None)
    {
        return std::unexpected(MakeError(
            AssetErrorCode::ReadFailed, resolvedPath, id, fastgltf::getErrorMessage(data.error())));
    }

    fastgltf::Parser parser;
    auto parsed = parser.loadGltfBinary(data.get(), resolvedPath.parent_path());
    if (parsed.error() != fastgltf::Error::None)
    {
        return std::unexpected(MakeError(
            AssetErrorCode::InvalidData, resolvedPath, id, fastgltf::getErrorMessage(parsed.error())));
    }

    fastgltf::Asset source = std::move(parsed.get());
    if (auto result = ValidateSupportedAsset(source, resolvedPath, id); !result)
    {
        return std::unexpected(std::move(result.error()));
    }
    if (const fastgltf::Error validationError = fastgltf::validate(source);
        validationError != fastgltf::Error::None)
    {
        return std::unexpected(MakeError(
            AssetErrorCode::InvalidData, resolvedPath, id, fastgltf::getErrorMessage(validationError)));
    }

    return ModelImporter(source, id, resolvedPath).Import();
}
}
