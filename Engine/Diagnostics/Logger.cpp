#include "Engine/Diagnostics/Logger.h"

#include <iostream>

namespace DeepRun::Diagnostics
{
namespace
{
constexpr std::string_view ToString(const LogCategory category)
{
    switch (category)
    {
    case LogCategory::Core: return "Core";
    case LogCategory::Platform: return "Platform";
    case LogCategory::Render: return "Render";
    case LogCategory::Physics: return "Physics";
    case LogCategory::Audio: return "Audio";
    case LogCategory::Input: return "Input";
    }
    return "Unknown";
}

constexpr std::string_view ToString(const LogLevel level)
{
    switch (level)
    {
    case LogLevel::Info: return "INFO";
    case LogLevel::Warning: return "WARN";
    case LogLevel::Error: return "ERROR";
    }
    return "UNKNOWN";
}
}

void Logger::Log(const LogCategory category, const LogLevel level, const std::string_view message)
{
    const std::scoped_lock lock(mutex_);
    std::ostream& output = level == LogLevel::Error ? std::cerr : std::cout;
    output << '[' << ToString(category) << "][" << ToString(level) << "] " << message << '\n';
    output.flush();
}

void Logger::Info(const LogCategory category, const std::string_view message)
{
    Log(category, LogLevel::Info, message);
}

void Logger::Warning(const LogCategory category, const std::string_view message)
{
    Log(category, LogLevel::Warning, message);
}

void Logger::Error(const LogCategory category, const std::string_view message)
{
    Log(category, LogLevel::Error, message);
}
}
