#pragma once

// Portable local-time helpers (avoid localtime_s / localtime_r #ifdefs in call sites).

#include <ctime>

namespace blockviz
{

// Fills |out| with local calendar time for |t|. Returns true on success.
inline bool local_time(std::tm& out, std::time_t t)
{
#if defined(_WIN32)
    return localtime_s(&out, &t) == 0;
#else
    return localtime_r(&t, &out) != nullptr;
#endif
}

} // namespace blockviz
