#include "graphics/pch.h"
#include "graphics/platform/gfx_platform.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <gdiplus.h>

#include "imgui.h"
#include "imgui_impl_win32.h"

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>

#include <cstdio>
#include <cstring>
#include <stdexcept>
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
} // namespace

uint32_t gfx_platform_surface_extension_names(const char** names, uint32_t capacity)
{
    if (!names || capacity < 2)
        return 0;
    names[0] = VK_KHR_SURFACE_EXTENSION_NAME;
    names[1] = VK_KHR_WIN32_SURFACE_EXTENSION_NAME;
    return 2;
}

VkSurfaceKHR gfx_platform_create_surface(VkInstance instance,
                                         void* window,
                                         void* platform_instance)
{
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkWin32SurfaceCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    createInfo.hwnd = static_cast<HWND>(window);
    createInfo.hinstance = static_cast<HINSTANCE>(platform_instance);

    if (vkCreateWin32SurfaceKHR(instance, &createInfo, nullptr, &surface) != VK_SUCCESS)
        throw std::runtime_error("Failed to create window surface");
    return surface;
}

void gfx_platform_destroy_surface(VkInstance instance, VkSurfaceKHR surface)
{
    vkDestroySurfaceKHR(instance, surface, nullptr);
}

void gfx_platform_get_window_size(void* window, uint32_t* out_w, uint32_t* out_h)
{
    uint32_t w = 0, h = 0;
    if (window)
    {
        RECT rect{};
        if (GetClientRect(static_cast<HWND>(window), &rect))
        {
            w = static_cast<uint32_t>(rect.right - rect.left);
            h = static_cast<uint32_t>(rect.bottom - rect.top);
        }
    }
    if (out_w)
        *out_w = w;
    if (out_h)
        *out_h = h;
}

void gfx_platform_imgui_init(void* window)
{
    ImGui_ImplWin32_Init(window);
}

void gfx_platform_imgui_shutdown()
{
    ImGui_ImplWin32_Shutdown();
}

void gfx_platform_imgui_new_frame()
{
    ImGui_ImplWin32_NewFrame();
}

void gfx_platform_debug_log(const char* msg)
{
    if (msg)
        OutputDebugStringA(msg);
}

bool gfx_platform_ensure_directory(const char* path_utf8)
{
    if (!path_utf8 || !path_utf8[0])
        return false;
    return CreateDirectoryA(path_utf8, nullptr) != 0 || GetLastError() == ERROR_ALREADY_EXISTS;
}

bool gfx_platform_save_window_png(void* window, const char* path_utf8)
{
    if (!window || !path_utf8 || !path_utf8[0])
        return false;

    HWND h = static_cast<HWND>(window);
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
            const std::wstring wpath = utf8_to_wide(path_utf8);
            ok = (bitmap.Save(wpath.c_str(), &clsid, nullptr) == Gdiplus::Ok);
        }
    }

    Gdiplus::GdiplusShutdown(gdiplusToken);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    return ok;
}
