#pragma once

// Portable env helpers (no platform TUs required).

#include <cstdlib>
#include <cstring>
#include <string>

namespace blockviz
{

// True if env var is set and not empty/"0"/"false"/"off" (case-insensitive for falsey words).
inline bool env_flag(const char* name, bool default_value = false)
{
    const char* v = std::getenv(name);
    if (!v || !v[0])
        return default_value;
    if (v[0] == '0' && v[1] == '\0')
        return false;
    if (std::strcmp(v, "false") == 0 || std::strcmp(v, "FALSE") == 0 ||
        std::strcmp(v, "off") == 0 || std::strcmp(v, "OFF") == 0 ||
        std::strcmp(v, "no") == 0 || std::strcmp(v, "NO") == 0)
        return false;
    return true;
}

inline std::string env_string(const char* name, const char* default_value = "")
{
    const char* v = std::getenv(name);
    if (!v || !v[0])
        return default_value ? std::string(default_value) : std::string();
    return std::string(v);
}

} // namespace blockviz
