#pragma once

#include <mutex>
#include <string_view>

namespace DeepRun::Diagnostics
{
enum class LogCategory
{
    Core,
    Platform,
    Render,
    Physics,
    Audio,
    Input,
};

enum class LogLevel
{
    Info,
    Warning,
    Error,
};

class Logger final
{
public:
    void Log(LogCategory category, LogLevel level, std::string_view message);
    void Info(LogCategory category, std::string_view message);
    void Warning(LogCategory category, std::string_view message);
    void Error(LogCategory category, std::string_view message);

private:
    std::mutex mutex_;
};
}
