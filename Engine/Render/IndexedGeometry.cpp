#include "Engine/Render/IndexedGeometry.h"

#include "Engine/Assets/ModelAsset.h"

#include <cstddef>
#include <limits>
#include <sstream>
#include <type_traits>

namespace DeepRun::Render
{
GpuModelHandle::GpuModelHandle(
    const std::uint64_t rendererIdentity,
    const std::size_t modelIndex) noexcept
    : rendererIdentity_(rendererIdentity), modelIndex_(modelIndex)
{
}

bool GpuModelHandle::IsValid() const noexcept
{
    return rendererIdentity_ != 0 && modelIndex_ != static_cast<std::size_t>(-1);
}

namespace
{
template <typename T>
bool FitsD3D12BufferView(const std::size_t count) noexcept
{
    return count <= std::numeric_limits<std::uint32_t>::max() / sizeof(T);
}
}

std::expected<IndexedGeometryLayout, std::string> BuildIndexedGeometryLayout(
    const Assets::ModelAsset& model)
{
    static_assert(std::is_standard_layout_v<Assets::MeshVertex>);
    static_assert(std::is_trivially_copyable_v<Assets::MeshVertex>);
    static_assert(sizeof(Assets::ModelVector3) == sizeof(float) * 3);
    static_assert(sizeof(Assets::MeshVertex) == sizeof(float) * 6);
    static_assert(offsetof(Assets::MeshVertex, position) == 0);
    static_assert(offsetof(Assets::MeshVertex, normal) == sizeof(float) * 3);

    if (model.primitives.empty())
    {
        return std::unexpected("indexed GPU model requires at least one primitive");
    }

    IndexedGeometryLayout layout;
    layout.primitives.reserve(model.primitives.size());
    for (std::size_t primitiveIndex = 0; primitiveIndex < model.primitives.size(); ++primitiveIndex)
    {
        const Assets::MeshPrimitiveData& primitive = model.primitives[primitiveIndex];
        if (primitive.vertices.empty() || primitive.indices.empty())
        {
            std::ostringstream message;
            message << "primitive " << primitiveIndex << " has empty indexed geometry";
            return std::unexpected(message.str());
        }
        if (!FitsD3D12BufferView<Assets::MeshVertex>(primitive.vertices.size()) ||
            !FitsD3D12BufferView<std::uint32_t>(primitive.indices.size()))
        {
            std::ostringstream message;
            message << "primitive " << primitiveIndex << " exceeds D3D12 buffer-view size limits";
            return std::unexpected(message.str());
        }

        const auto vertexCount = static_cast<std::uint32_t>(primitive.vertices.size());
        const auto indexCount = static_cast<std::uint32_t>(primitive.indices.size());
        const auto vertexBytes = static_cast<std::uint32_t>(primitive.vertices.size() * sizeof(Assets::MeshVertex));
        const auto indexBytes = static_cast<std::uint32_t>(primitive.indices.size() * sizeof(std::uint32_t));
        layout.primitives.push_back({
            .primitiveIndex = primitiveIndex,
            .vertexCount = vertexCount,
            .indexCount = indexCount,
            .vertexBytes = vertexBytes,
            .indexBytes = indexBytes});
        layout.totals.vertexCount += vertexCount;
        layout.totals.indexCount += indexCount;
        layout.totals.vertexBytes += vertexBytes;
        layout.totals.indexBytes += indexBytes;
    }
    layout.totals.primitiveCount = layout.primitives.size();
    return layout;
}
}
