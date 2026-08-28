#include "Engine/Assets/AssetManager.h"

#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

namespace DeepRun::Assets
{
AssetId::AssetId(std::string value)
    : value_(std::move(value))
{
}

std::expected<AssetId, std::string> AssetId::FromPath(const std::filesystem::path& relativePath)
{
    if (relativePath.empty() || relativePath.is_absolute() || relativePath.has_root_path())
    {
        return std::unexpected("asset path must be a non-empty relative path");
    }

    const std::filesystem::path normalized = relativePath.lexically_normal();
    for (const std::filesystem::path& component : normalized)
    {
        if (component == "..")
        {
            return std::unexpected("asset path must stay inside the asset root");
        }
    }

    std::string value = normalized.generic_string();
    if (value.empty() || value == ".")
    {
        return std::unexpected("asset path must identify a file");
    }
    return AssetId(std::move(value));
}

std::string_view AssetId::Value() const noexcept
{
    return value_;
}

AssetManager::AssetManager(std::filesystem::path root)
{
    std::error_code error;
    root_ = std::filesystem::absolute(std::move(root), error).lexically_normal();
    if (error)
    {
        root_.clear();
    }
}

std::expected<AssetHandle<TextAsset>, AssetError> AssetManager::LoadText(
    const std::filesystem::path& relativePath)
{
    const auto idResult = AssetId::FromPath(relativePath);
    if (!idResult)
    {
        return std::unexpected(AssetError{AssetErrorCode::InvalidPath, relativePath, idResult.error()});
    }

    const AssetId id = *idResult;
    const std::string key(id.Value());
    if (const auto found = textCache_.find(key); found != textCache_.end())
    {
        return AssetHandle<TextAsset>(found->second);
    }

    const std::filesystem::path resolvedPath = Resolve(id);
    std::ifstream input(resolvedPath, std::ios::binary);
    if (!input)
    {
        return std::unexpected(AssetError{
            std::filesystem::exists(resolvedPath) ? AssetErrorCode::ReadFailed : AssetErrorCode::NotFound,
            resolvedPath,
            "unable to open text asset"});
    }

    std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (input.bad())
    {
        return std::unexpected(AssetError{AssetErrorCode::ReadFailed, resolvedPath, "failed while reading text asset"});
    }

    auto asset = std::make_shared<const TextAsset>(TextAsset{id, std::move(contents)});
    textCache_.emplace(key, asset);
    return AssetHandle<TextAsset>(asset);
}

std::filesystem::path AssetManager::Resolve(const AssetId& id) const
{
    return (root_ / std::filesystem::path(id.Value())).lexically_normal();
}

const std::filesystem::path& AssetManager::Root() const noexcept
{
    return root_;
}

std::size_t AssetManager::CachedResourceCount() const noexcept
{
    return textCache_.size();
}

void AssetManager::Clear() noexcept
{
    textCache_.clear();
}
}
