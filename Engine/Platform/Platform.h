#pragma once

#include <filesystem>
#include <cstdint>

namespace DeepRun::Platform
{
[[nodiscard]] std::filesystem::path ExecutablePath();
struct ProcessMemory final
{
    std::uint64_t workingSetBytes = 0;
    std::uint64_t privateCommitBytes = 0;
    bool available = false;
};
[[nodiscard]] ProcessMemory QueryProcessMemory() noexcept;
}
