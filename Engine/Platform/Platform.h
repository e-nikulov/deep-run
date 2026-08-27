#pragma once

#include <filesystem>

namespace DeepRun::Platform
{
[[nodiscard]] std::filesystem::path ExecutablePath();
}
