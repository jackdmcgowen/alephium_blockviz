#include "network/platform/net_platform.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>

#include <cstdlib>
#include <string>

#pragma comment(lib, "psapi.lib")

std::string net_platform_cache_root()
{
    char* buf = nullptr;
    size_t len = 0;
    if (_dupenv_s(&buf, &len, "LOCALAPPDATA") == 0 && buf && buf[0])
    {
        std::string s(buf);
        free(buf);
        // Use forward slashes; std::filesystem accepts them on Windows.
        return s + "/AlephiumBlockViz/cache";
    }
    if (buf)
        free(buf);
    return "./AlephiumBlockViz/cache";
}

bool net_platform_process_private_bytes(size_t* out_bytes)
{
    if (!out_bytes)
        return false;
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    if (!GetProcessMemoryInfo(GetCurrentProcess(),
                              reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                              sizeof(pmc)))
        return false;
    *out_bytes = static_cast<size_t>(pmc.PrivateUsage);
    return true;
}
