#pragma once

// OS-specific network helpers. One of net_platform_win32.cpp / net_platform_linux.cpp
// is linked per build — do not call OS APIs from shared network TUs.

#include <cstddef>
#include <string>

// Root for segment disk cache (no domain suffix). Caller appends domain key.
// Windows: %LOCALAPPDATA%/AlephiumBlockViz/cache
// Linux:   $XDG_CACHE_HOME/AlephiumBlockViz/cache or ~/.cache/AlephiumBlockViz/cache
// Fallback: ./AlephiumBlockViz/cache
std::string net_platform_cache_root();

// Process private working set / equivalent. Returns false if unavailable.
bool net_platform_process_private_bytes(size_t* out_bytes);
