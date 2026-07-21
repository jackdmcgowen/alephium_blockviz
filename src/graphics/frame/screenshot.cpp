#include "graphics/pch.h"
#include "graphics/graphics_system.hpp"
#include "graphics/platform/gfx_platform.hpp"

#include <cstdio>
#include <ctime>
#include <string>

namespace
{
std::string make_default_path()
{
    gfx_platform_ensure_directory("docs");
    gfx_platform_ensure_directory("docs/images");

    const std::time_t t = std::time(nullptr);
    std::tm tm_local{};
#if defined(_WIN32)
    localtime_s(&tm_local, &t);
#else
    localtime_r(&t, &tm_local);
#endif
    char name[256]{};
    std::snprintf(name, sizeof(name), "docs/images/capture_%04d%02d%02d_%02d%02d%02d.png",
                  tm_local.tm_year + 1900, tm_local.tm_mon + 1, tm_local.tm_mday,
                  tm_local.tm_hour, tm_local.tm_min, tm_local.tm_sec);
    return name;
}
} // namespace

void GraphicsSystem::request_screenshot(const char* path_utf8)
{
    std::lock_guard<std::mutex> lock(screenshot_mutex_);
    if (path_utf8 && path_utf8[0])
        screenshot_pending_path_ = path_utf8;
    else
        screenshot_pending_path_ = make_default_path();
}

bool GraphicsSystem::consume_and_save_screenshot_()
{
    std::string path;
    {
        std::lock_guard<std::mutex> lock(screenshot_mutex_);
        if (screenshot_pending_path_.empty())
            return false;
        path = std::move(screenshot_pending_path_);
        screenshot_pending_path_.clear();
    }

    if (!hwnd)
    {
        std::printf("[gfx] screenshot: no window\n");
        return false;
    }

    // Parent dir for custom paths — best-effort.
    {
        const auto slash = path.find_last_of("/\\");
        if (slash != std::string::npos)
        {
            const std::string dir = path.substr(0, slash);
            if (!dir.empty())
                gfx_platform_ensure_directory(dir.c_str());
        }
    }

    const bool ok = gfx_platform_save_window_png(hwnd, path.c_str());
    if (ok)
        std::printf("[gfx] screenshot saved: %s\n", path.c_str());
    else
        std::printf("[gfx] screenshot failed: %s\n", path.c_str());
    return ok;
}
