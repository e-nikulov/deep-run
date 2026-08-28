#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace DeepRun::Assets
{
struct ModelAsset;
}

namespace DeepRun::Render
{
class D3D12Renderer;

class GpuModelHandle final
{
public:
    GpuModelHandle() = default;

    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] bool operator==(const GpuModelHandle&) const noexcept = default;

private:
    GpuModelHandle(std::uint64_t rendererIdentity, std::size_t modelIndex) noexcept;

    std::uint64_t rendererIdentity_ = 0;
    std::size_t modelIndex_ = static_cast<std::size_t>(-1);

    friend class D3D12Renderer;
};

struct IndexedPrimitiveLayout final
{
    std::size_t primitiveIndex = 0;
    std::uint32_t vertexCount = 0;
    std::uint32_t indexCount = 0;
    std::uint32_t vertexBytes = 0;
    std::uint32_t indexBytes = 0;
};

struct GpuModelUploadStats final
{
    std::size_t primitiveCount = 0;
    std::uint64_t vertexCount = 0;
    std::uint64_t indexCount = 0;
    std::uint64_t vertexBytes = 0;
    std::uint64_t indexBytes = 0;
    bool uploadCompleted = false;

    [[nodiscard]] bool operator==(const GpuModelUploadStats&) const noexcept = default;
};

struct GpuModelUploadResult final
{
    GpuModelHandle handle;
    GpuModelUploadStats stats;
};

struct IndexedGeometryLayout final
{
    std::vector<IndexedPrimitiveLayout> primitives;
    GpuModelUploadStats totals;
};

[[nodiscard]] std::expected<IndexedGeometryLayout, std::string> BuildIndexedGeometryLayout(
    const Assets::ModelAsset& model);
}
