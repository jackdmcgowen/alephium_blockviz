#include "graphics/pch.h"
#include "graphics/graphics_system.hpp"

#include <windows.h>
#include <gdiplus.h>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

namespace
{
int get_encoder_clsid(const WCHAR* format, CLSID* pClsid)
{
    UINT num = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0)
        return -1;
    std::vector<BYTE> buf(size);
    auto* info = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buf.data());
    Gdiplus::GetImageEncoders(num, size, info);
    for (UINT i = 0; i < num; ++i)
    {
        if (wcscmp(info[i].MimeType, format) == 0)
        {
            *pClsid = info[i].Clsid;
            return static_cast<int>(i);
        }
    }
    return -1;
}

std::wstring utf8_to_wide(const char* utf8)
{
    if (!utf8 || !utf8[0])
        return L"";
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    if (n <= 0)
        return L"";
    std::wstring out(static_cast<size_t>(n - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out.data(), n);
    return out;
}

std::string make_default_path()
{
    char path[MAX_PATH]{};
    // Prefer docs/images under cwd (repo root when launched correctly).
    std::snprintf(path, sizeof(path), "docs\\images");
    CreateDirectoryA(path, nullptr);

    const std::time_t t = std::time(nullptr);
    std::tm tm_local{};
    localtime_s(&tm_local, &t);
    char name[MAX_PATH]{};
    std::snprintf(name, sizeof(name), "docs\\images\\capture_%04d%02d%02d_%02d%02d%02d.png",
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
        std::printf("[gfx] screenshot: no hwnd\n");
        return false;
    }

    HWND h = static_cast<HWND>(hwnd);
    RECT rc{};
    if (!GetClientRect(h, &rc))
        return false;
    POINT tl{ rc.left, rc.top };
    POINT br{ rc.right, rc.bottom };
    ClientToScreen(h, &tl);
    ClientToScreen(h, &br);
    const int w = br.x - tl.x;
    const int ht = br.y - tl.y;
    if (w < 2 || ht < 2)
        return false;

    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, w, ht);
    HGDIOBJ old = SelectObject(mem, bmp);
    BitBlt(mem, 0, 0, w, ht, screen, tl.x, tl.y, SRCCOPY);
    SelectObject(mem, old);

    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken = 0;
    if (Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr) != Gdiplus::Ok)
    {
        DeleteObject(bmp);
        DeleteDC(mem);
        ReleaseDC(nullptr, screen);
        std::printf("[gfx] screenshot: GDI+ startup failed\n");
        return false;
    }

    bool ok = false;
    {
        Gdiplus::Bitmap bitmap(bmp, nullptr);
        CLSID clsid{};
        if (get_encoder_clsid(L"image/png", &clsid) >= 0)
        {
            const std::wstring wpath = utf8_to_wide(path.c_str());
            ok = (bitmap.Save(wpath.c_str(), &clsid, nullptr) == Gdiplus::Ok);
        }
    }

    Gdiplus::GdiplusShutdown(gdiplusToken);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);

    if (ok)
        std::printf("[gfx] screenshot saved: %s\n", path.c_str());
    else
        std::printf("[gfx] screenshot failed: %s\n", path.c_str());
    return ok;
}
