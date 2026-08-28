#pragma once

#include "Engine/Assets/AssetManager.h"
#include "Engine/Assets/ModelAsset.h"

#include <expected>
#include <filesystem>

namespace DeepRun::Assets
{
[[nodiscard]] std::expected<ModelAsset, AssetError> LoadGltfModel(
    const AssetId& id,
    const std::filesystem::path& resolvedPath);
}
