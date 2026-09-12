from pathlib import Path

p = Path("Engine/Assets/GltfModelLoader.cpp")
s = p.read_text(encoding="utf-8")

def replace_once(old: str, new: str) -> None:
    global s
    count = s.count(old)
    if count != 1:
        raise SystemExit(f"expected one match, got {count}: {old[:100]!r}")
    s = s.replace(old, new, 1)

replace_once(
'''    if (!asset.animations.empty() || !asset.skins.empty())
    {
        return std::unexpected(MakeError(
            AssetErrorCode::UnsupportedData, path, id, "animations and skins are outside the C0 model subset"));
    }
''',
'''    if (!asset.skins.empty())
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
''')

replace_once(
'''        : source_(source), id_(id), path_(path), output_{id, {}, {}, {}, {}, {}},
          meshPrimitiveCache_(source.meshes.size()), nodeVisitState_(source.nodes.size(), 0)
''',
'''        : source_(source), id_(id), path_(path), output_{id, {}, {}, {}, {}, {}, {}},
          meshPrimitiveCache_(source.meshes.size()), nodeVisitState_(source.nodes.size(), 0)
''')

replace_once(
'''        for (const std::size_t nodeIndex : source_.scenes[sceneIndex].nodeIndices)
        {
            if (auto result = VisitNode(nodeIndex, fastgltf::math::fmat4x4()); !result)
            {
                return std::unexpected(std::move(result.error()));
            }
        }

        if (output_.nodes.empty() || output_.primitives.empty() || !modelBoundsInitialized_)
''',
'''        for (const std::size_t nodeIndex : source_.scenes[sceneIndex].nodeIndices)
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
''')

anchor = '''private:
    ValidationResult ImportMaterials()
'''
if s.count(anchor) != 1:
    raise SystemExit("ImportMaterials anchor mismatch")
method = r'''private:
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
'''
s = s.replace(anchor, method, 1)

p.write_text(s, encoding="utf-8")
print("bounded glTF animation patch applied")
