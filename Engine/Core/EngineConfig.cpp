#include "Engine/Core/EngineConfig.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <limits>
#include <string_view>

namespace DeepRun::Core
{
namespace
{
using Json = nlohmann::json;

std::expected<std::uint32_t, std::string> ReadPositiveInteger(
    const Json& parent,
    const std::string_view name,
    const std::string_view qualifiedName)
{
    const auto found = parent.find(name);
    if (found == parent.end() || !found->is_number_integer())
    {
        return std::unexpected(std::string(qualifiedName) + " must be a positive integer");
    }

    if (found->is_number_unsigned())
    {
        const std::uint64_t value = found->get<std::uint64_t>();
        if (value == 0 || value > std::numeric_limits<std::uint32_t>::max())
        {
            return std::unexpected(std::string(qualifiedName) + " is outside the supported range");
        }
        return static_cast<std::uint32_t>(value);
    }

    const std::int64_t value = found->get<std::int64_t>();
    if (value <= 0 || value > std::numeric_limits<std::uint32_t>::max())
    {
        return std::unexpected(std::string(qualifiedName) + " is outside the supported range");
    }
    return static_cast<std::uint32_t>(value);
}
}

std::expected<EngineConfig, ConfigError> LoadEngineConfig(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        return std::unexpected(ConfigError{
            std::filesystem::exists(path) ? ConfigErrorCode::ReadFailed : ConfigErrorCode::FileNotFound,
            path,
            "unable to open engine configuration"});
    }

    try
    {
        Json document;
        input >> document;
        if (input.bad())
        {
            return std::unexpected(
                ConfigError{ConfigErrorCode::ReadFailed, path, "failed while reading engine configuration"});
        }
        if (!document.is_object())
        {
            return std::unexpected(
                ConfigError{ConfigErrorCode::InvalidValue, path, "configuration root must be an object"});
        }

        const auto renderer = document.find("renderer");
        if (renderer == document.end() || !renderer->is_object())
        {
            return std::unexpected(ConfigError{ConfigErrorCode::InvalidValue, path, "renderer must be an object"});
        }
        const auto physics = document.find("physics");
        if (physics == document.end() || !physics->is_object())
        {
            return std::unexpected(ConfigError{ConfigErrorCode::InvalidValue, path, "physics must be an object"});
        }

        EngineConfig config;
        const auto width = ReadPositiveInteger(*renderer, "width", "renderer.width");
        const auto height = ReadPositiveInteger(*renderer, "height", "renderer.height");
        const auto fixedHz = ReadPositiveInteger(*physics, "fixedHz", "physics.fixedHz");
        if (!width || !height || !fixedHz)
        {
            const std::string& message = !width ? width.error() : (!height ? height.error() : fixedHz.error());
            return std::unexpected(ConfigError{ConfigErrorCode::InvalidValue, path, message});
        }

        const auto vsync = renderer->find("vsync");
        if (vsync == renderer->end() || !vsync->is_boolean())
        {
            return std::unexpected(
                ConfigError{ConfigErrorCode::InvalidValue, path, "renderer.vsync must be a boolean"});
        }
        const auto hdr = renderer->find("hdr");
        if (hdr != renderer->end() && !hdr->is_boolean())
        {
            return std::unexpected(
                ConfigError{ConfigErrorCode::InvalidValue, path, "renderer.hdr must be a boolean"});
        }
        if (*fixedHz > 1'000U)
        {
            return std::unexpected(
                ConfigError{ConfigErrorCode::InvalidValue, path, "physics.fixedHz must not exceed 1000"});
        }

        config.renderer.width = *width;
        config.renderer.height = *height;
        config.renderer.vsync = vsync->get<bool>();
        config.renderer.hdr = hdr != renderer->end() && hdr->get<bool>();
        config.physics.fixedHz = *fixedHz;
        return config;
    }
    catch (const Json::parse_error& error)
    {
        return std::unexpected(ConfigError{ConfigErrorCode::InvalidJson, path, error.what()});
    }
    catch (const Json::exception& error)
    {
        return std::unexpected(
            ConfigError{ConfigErrorCode::InvalidValue, path, std::string("JSON configuration error: ") + error.what()});
    }
}
}
