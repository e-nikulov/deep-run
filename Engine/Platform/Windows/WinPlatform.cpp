#include "Engine/Platform/Platform.h"

#include <Windows.h>
#include <Psapi.h>

#include <system_error>
#include <vector>

namespace DeepRun::Platform
{
ProcessMemory QueryProcessMemory() noexcept
{
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (!K32GetProcessMemoryInfo(GetCurrentProcess(),
            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters))) return {};
    return {counters.WorkingSetSize, counters.PrivateUsage, true};
}

std::filesystem::path ExecutablePath()
{
    std::vector<wchar_t> buffer(32'768);
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length == buffer.size())
    {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "GetModuleFileNameW failed");
    }
    return std::filesystem::path(std::wstring_view(buffer.data(), length));
}
}
