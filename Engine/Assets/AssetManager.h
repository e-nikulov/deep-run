#pragma once

#include <cassert>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace DeepRun::Assets
{
class AssetManager;

template <typename T>
class AssetHandle final
{
public:
    AssetHandle() = default;

    [[nodiscard]] bool IsValid() const noexcept
    {
        return !asset_.expired();
    }

    [[nodiscard]] const T* Get() const noexcept
    {
        const std::shared_ptr<const T> asset = asset_.lock();
        return asset != nullptr ? asset.get() : nullptr;
    }

    [[nodiscard]] const T& operator*() const noexcept
    {
        const T* asset = Get();
        assert(asset != nullptr);
        return *asset;
    }

    [[nodiscard]] const T* operator->() const noexcept
    {
        const T* asset = Get();
        assert(asset != nullptr);
        return asset;
    }

    [[nodiscard]] bool operator==(const AssetHandle& other) const noexcept
    {
        return !asset_.owner_before(other.asset_) && !other.asset_.owner_before(asset_);
    }

private:
    explicit AssetHandle(const std::shared_ptr<const T>& asset) noexcept
        : asset_(asset)
    {
    }

    std::weak_ptr<const T> asset_;

    friend class AssetManager;
};

class AssetId final
{
public:
    [[nodiscard]] static std::expected<AssetId, std::string> FromPath(const std::filesystem::path& relativePath);

    [[nodiscard]] std::string_view Value() const noexcept;
    [[nodiscard]] bool operator==(const AssetId&) const noexcept = default;

private:
    explicit AssetId(std::string value);
    std::string value_;
};

struct TextAsset final
{
    AssetId id;
    std::string text;
};

enum class AssetErrorCode
{
    InvalidPath,
    NotFound,
    ReadFailed,
};

struct AssetError final
{
    AssetErrorCode code = AssetErrorCode::ReadFailed;
    std::filesystem::path path;
    std::string message;
};

class AssetManager final
{
public:
    explicit AssetManager(std::filesystem::path root);

    [[nodiscard]] std::expected<AssetHandle<TextAsset>, AssetError> LoadText(
        const std::filesystem::path& relativePath);
    [[nodiscard]] std::filesystem::path Resolve(const AssetId& id) const;
    [[nodiscard]] const std::filesystem::path& Root() const noexcept;
    [[nodiscard]] std::size_t CachedResourceCount() const noexcept;
    void Clear() noexcept;

private:
    std::filesystem::path root_;
    std::unordered_map<std::string, std::shared_ptr<const TextAsset>> textCache_;
};
}
