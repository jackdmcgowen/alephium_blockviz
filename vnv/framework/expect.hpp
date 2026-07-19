#pragma once

// Thin VnV assert helpers (mod / optional int CPU checks).
// No third-party test framework required.

#include <cstdio>

struct VnvStats
{
    int passes = 0;
    int fails  = 0;
};

inline void vnv_expect(bool cond, const char* msg, VnvStats& s)
{
    if (cond)
    {
        std::printf("ok: %s\n", msg);
        ++s.passes;
    }
    else
    {
        std::printf("FAIL: %s\n", msg);
        ++s.fails;
    }
}

#define VNV_EXPECT(stats, cond) ::vnv_expect(!!(cond), #cond, (stats))
#define VNV_EXPECT_MSG(stats, cond, msg) ::vnv_expect(!!(cond), (msg), (stats))
