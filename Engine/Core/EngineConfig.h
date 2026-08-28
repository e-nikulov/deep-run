#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>

namespace DeepRun::Core
{
struct RendererConfig final
{
    bool vsync = true;
    std::uint32_t width = 1280;
    std::uint32_t height = 720;
};

struct PhysicsConfig final
{
    std::uint32_t fixedHz = 60;
};

struct EngineConfig final
{
    RendererConfig renderer;
    PhysicsConfig physics;
};

enum class ConfigErrorCode
{
    FileNotFound,
    ReadFailed,
    InvalidJson,
    InvalidValue,
};

struct ConfigError final
{
    ConfigErrorCode code = ConfigErrorCode::InvalidValue;
    std::filesystem::path path;
    std::string message;
};

[[nodiscard]] std::expected<EngineConfig, ConfigError> LoadEngineConfig(const std::filesystem::path& path);
}
