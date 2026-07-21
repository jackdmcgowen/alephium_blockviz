#include "network/platform/net_platform.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

std::string net_platform_cache_root()
{
    if (const char* xdg = std::getenv("XDG_CACHE_HOME"))
    {
        if (xdg[0])
            return std::string(xdg) + "/AlephiumBlockViz/cache";
    }
    if (const char* home = std::getenv("HOME"))
    {
        if (home[0])
            return std::string(home) + "/.cache/AlephiumBlockViz/cache";
    }
    return "./AlephiumBlockViz/cache";
}

bool net_platform_process_private_bytes(size_t* out_bytes)
{
    if (!out_bytes)
        return false;
    std::ifstream in("/proc/self/status");
    if (!in)
        return false;
    std::string key;
    // Prefer VmRSS (resident) as a practical pressure signal.
    while (in >> key)
    {
        if (key == "VmRSS:")
        {
            size_t kb = 0;
            in >> kb;
            *out_bytes = kb * 1024ull;
            return true;
        }
        // skip rest of line
        std::string rest;
        std::getline(in, rest);
    }
    return false;
}
