#include "Engine/Render/ModelDraw.h"

#include <cmath>
#include <optional>
#include <sstream>
#include <utility>
#include <vector>

namespace DeepRun::Render
{
namespace
{
float Element(const Assets::ModelTransform& matrix, const std::size_t row, const std::size_t column) noexcept
{
    return matrix.values[column * 4 + row];
}

void SetElement(
    Assets::ModelTransform& matrix,
    const std::size_t row,
    const std::size_t column,
    const float value) noexcept
{
    matrix.values[column * 4 + row] = value;
}

bool IsFiniteAffineTransform(const Assets::ModelTransform& transform) noexcept
{
    for (const float value : transform.values)
    {
        if (!std::isfinite(value))
        {
            return false;
        }
    }
    return transform.values[3] == 0.0F && transform.values[7] == 0.0F &&
           transform.values[11] == 0.0F && transform.values[15] == 1.0F;
}

std::optional<Assets::ModelTransform> InvertAffineTransform(const Assets::ModelTransform& transform) noexcept
{
    if (!IsFiniteAffineTransform(transform))
    {
        return std::nullopt;
    }

    const float a00 = Element(transform, 0, 0);
    const float a01 = Element(transform, 0, 1);
    const float a02 = Element(transform, 0, 2);
    const float a10 = Element(transform, 1, 0);
    const float a11 = Element(transform, 1, 1);
    const float a12 = Element(transform, 1, 2);
    const float a20 = Element(transform, 2, 0);
    const float a21 = Element(transform, 2, 1);
    const float a22 = Element(transform, 2, 2);
    const float determinant =
        a00 * (a11 * a22 - a12 * a21) -
        a01 * (a10 * a22 - a12 * a20) +
        a02 * (a10 * a21 - a11 * a20);
    if (!std::isfinite(determinant) || std::abs(determinant) < 1.0e-8F)
    {
        return std::nullopt;
    }

    const float inverseDeterminant = 1.0F / determinant;
    const float inverseLinear[3][3]{
        {(a11 * a22 - a12 * a21) * inverseDeterminant,
         (a02 * a21 - a01 * a22) * inverseDeterminant,
         (a01 * a12 - a02 * a11) * inverseDeterminant},
        {(a12 * a20 - a10 * a22) * inverseDeterminant,
         (a00 * a22 - a02 * a20) * inverseDeterminant,
         (a02 * a10 - a00 * a12) * inverseDeterminant},
        {(a10 * a21 - a11 * a20) * inverseDeterminant,
         (a01 * a20 - a00 * a21) * inverseDeterminant,
         (a00 * a11 - a01 * a10) * inverseDeterminant}};

    Assets::ModelTransform inverse{};
    for (std::size_t row = 0; row < 3; ++row)
    {
        for (std::size_t column = 0; column < 3; ++column)
        {
            SetElement(inverse, row, column, inverseLinear[row][column]);
        }
    }
    const float tx = Element(transform, 0, 3);
    const float ty = Element(transform, 1, 3);
    const float tz = Element(transform, 2, 3);
    for (std::size_t row = 0; row < 3; ++row)
    {
        SetElement(
            inverse, row, 3,
            -(inverseLinear[row][0] * tx + inverseLinear[row][1] * ty + inverseLinear[row][2] * tz));
    }
    return inverse;
}
}

Assets::ModelMaterialData DefaultModelMaterial()
{
    return {
        .name = "DefaultNeutral",
        .baseColorFactor = {0.5F, 0.5F, 0.5F, 1.0F},
        .metallicFactor = 0.0F,
        .roughnessFactor = 1.0F};
}

Assets::ModelTransform Multiply(
    const Assets::ModelTransform& left,
    const Assets::ModelTransform& right) noexcept
{
    Assets::ModelTransform result;
    result.values.fill(0.0F);
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            float value = 0.0F;
            for (std::size_t inner = 0; inner < 4; ++inner)
            {
                value += Element(left, row, inner) * Element(right, inner, column);
            }
            SetElement(result, row, column, value);
        }
    }
    return result;
}

std::expected<NormalTransform, std::string> BuildNormalTransform(const Assets::ModelTransform& transform)
{
    const float a00 = Element(transform, 0, 0);
    const float a01 = Element(transform, 0, 1);
    const float a02 = Element(transform, 0, 2);
    const float a10 = Element(transform, 1, 0);
    const float a11 = Element(transform, 1, 1);
    const float a12 = Element(transform, 1, 2);
    const float a20 = Element(transform, 2, 0);
    const float a21 = Element(transform, 2, 1);
    const float a22 = Element(transform, 2, 2);
    const float determinant =
        a00 * (a11 * a22 - a12 * a21) -
        a01 * (a10 * a22 - a12 * a20) +
        a02 * (a10 * a21 - a11 * a20);
    if (!std::isfinite(determinant) || std::abs(determinant) < 1.0e-8F)
    {
        return std::unexpected("model draw transform has a singular or non-finite normal matrix");
    }

    const float inverseDeterminant = 1.0F / determinant;
    const float inverse[3][3]{
        {(a11 * a22 - a12 * a21) * inverseDeterminant,
         (a02 * a21 - a01 * a22) * inverseDeterminant,
         (a01 * a12 - a02 * a11) * inverseDeterminant},
        {(a12 * a20 - a10 * a22) * inverseDeterminant,
         (a00 * a22 - a02 * a20) * inverseDeterminant,
         (a02 * a10 - a00 * a12) * inverseDeterminant},
        {(a10 * a21 - a11 * a20) * inverseDeterminant,
         (a01 * a20 - a00 * a21) * inverseDeterminant,
         (a00 * a11 - a01 * a10) * inverseDeterminant}};

    NormalTransform normal;
    for (std::size_t row = 0; row < 3; ++row)
    {
        for (std::size_t column = 0; column < 3; ++column)
        {
            normal.rows[row * 4 + column] = inverse[column][row];
        }
    }
    return normal;
}

std::array<float, 3> TransformNormal(
    const NormalTransform& transform,
    const std::array<float, 3>& normal) noexcept
{
    std::array<float, 3> result{};
    for (std::size_t row = 0; row < 3; ++row)
    {
        for (std::size_t column = 0; column < 3; ++column)
        {
            result[row] += transform.rows[row * 4 + column] * normal[column];
        }
    }
    return result;
}

std::expected<std::vector<ModelDrawInstance>, std::string> PrepareModelDraws(
    const Assets::ModelAsset& model,
    const Assets::ModelTransform& modelToWorld,
    const std::span<const ModelNodeTransformOverride> nodeTransformOverrides,
    const std::span<const ModelBindingTransformOverride> bindingTransformOverrides)
{
    std::vector<const Assets::ModelTransform*> overridesByNode(model.nodes.size(), nullptr);
    for (const ModelNodeTransformOverride& overrideValue : nodeTransformOverrides)
    {
        if (overrideValue.nodeIndex >= model.nodes.size())
        {
            return std::unexpected("model node transform override index is out of range");
        }
        if (overridesByNode[overrideValue.nodeIndex] != nullptr)
        {
            return std::unexpected("model node transform overrides contain a duplicate node index");
        }
        if (!IsFiniteAffineTransform(overrideValue.nodeLocalPostTransform))
        {
            return std::unexpected("model node transform override must be finite and affine");
        }
        overridesByNode[overrideValue.nodeIndex] = &overrideValue.nodeLocalPostTransform;
    }

    std::vector<std::optional<Assets::ModelTransform>> bindingTransformsByNode(model.nodes.size());
    for (const ModelBindingTransformOverride& overrideValue : bindingTransformOverrides)
    {
        if (overrideValue.bindingIndex >= model.nodeBindings.size())
        {
            return std::unexpected("model binding transform override index is out of range");
        }
        if (!IsFiniteAffineTransform(overrideValue.bindingLocalPostTransform))
        {
            return std::unexpected("model binding transform override must be finite and affine");
        }
        const Assets::ModelNodeBindingData& binding = model.nodeBindings[overrideValue.bindingIndex];
        if (binding.drawableMeshNodeIndices.empty())
        {
            return std::unexpected("model binding transform override has no drawable descendants");
        }
        const auto inverseRoot = InvertAffineTransform(binding.localToModel);
        if (!inverseRoot.has_value())
        {
            return std::unexpected("model binding transform override root is singular or non-finite");
        }
        const Assets::ModelTransform modelSpacePresentation = Multiply(
            Multiply(binding.localToModel, overrideValue.bindingLocalPostTransform), *inverseRoot);
        for (const std::size_t meshNodeIndex : binding.drawableMeshNodeIndices)
        {
            if (meshNodeIndex >= model.nodes.size())
            {
                return std::unexpected("model binding transform override references an invalid drawable node");
            }
            if (overridesByNode[meshNodeIndex] != nullptr || bindingTransformsByNode[meshNodeIndex].has_value())
            {
                return std::unexpected("model transform overrides overlap on a drawable node");
            }
            bindingTransformsByNode[meshNodeIndex] = modelSpacePresentation;
        }
    }

    std::vector<ModelDrawInstance> draws;
    for (std::size_t nodeIndex = 0; nodeIndex < model.nodes.size(); ++nodeIndex)
    {
        const Assets::MeshNodeData& node = model.nodes[nodeIndex];
        Assets::ModelTransform combined = bindingTransformsByNode[nodeIndex].has_value()
            ? Multiply(Multiply(modelToWorld, *bindingTransformsByNode[nodeIndex]), node.localToModel)
            : Multiply(modelToWorld, node.localToModel);
        if (overridesByNode[nodeIndex] != nullptr)
        {
            // POST-transform is intentional: modelToWorld * authored node transform * presentation transform.
            combined = Multiply(combined, *overridesByNode[nodeIndex]);
        }
        const auto normal = BuildNormalTransform(combined);
        if (!normal)
        {
            std::ostringstream message;
            message << "node " << nodeIndex << ": " << normal.error();
            return std::unexpected(message.str());
        }

        for (const std::size_t primitiveIndex : node.primitiveIndices)
        {
            if (primitiveIndex >= model.primitives.size())
            {
                std::ostringstream message;
                message << "node " << nodeIndex << " references invalid primitive " << primitiveIndex;
                return std::unexpected(message.str());
            }

            const Assets::MeshPrimitiveData& primitive = model.primitives[primitiveIndex];
            Assets::ModelMaterialData material = DefaultModelMaterial();
            if (primitive.materialIndex.has_value())
            {
                if (*primitive.materialIndex >= model.materials.size())
                {
                    std::ostringstream message;
                    message << "primitive " << primitiveIndex << " references invalid material "
                            << *primitive.materialIndex;
                    return std::unexpected(message.str());
                }
                material = model.materials[*primitive.materialIndex];
            }

            draws.push_back({
                .nodeIndex = nodeIndex,
                .primitiveIndex = primitiveIndex,
                .modelToWorld = combined,
                .normalToWorld = *normal,
                .material = std::move(material)});
        }
    }
    if (draws.empty())
    {
        return std::unexpected("model contains no node-referenced primitives to draw");
    }
    return draws;
}
}
