#pragma once

#include <string>

namespace DeepRun::Scene
{
// Right-handed world space: +X right, +Y up, +Z toward the camera. The default
// side-view camera looks along -Z, and XY is the primary 2.5D gameplay plane.
struct Float3 final
{
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;

    [[nodiscard]] constexpr bool operator==(const Float3&) const noexcept = default;
};

struct Quaternion final
{
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float w = 1.0F;

    [[nodiscard]] constexpr bool operator==(const Quaternion&) const noexcept = default;
};

struct Transform final
{
    Float3 position{};
    Quaternion rotation{};
    Float3 scale{1.0F, 1.0F, 1.0F};
};

struct Tag final
{
    std::string name;
};
}
