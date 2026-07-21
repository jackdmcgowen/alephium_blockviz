#include "app/pch.h"
#include "app/platform/app_platform.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdio>

// Linked via graphics.lib. Do not include gfx_platform.hpp (pulls Vulkan headers;
// app project has no VULKAN_SDK include path).
void gfx_platform_configure_headless(bool enabled, uint32_t width, uint32_t height);

// ImGui Win32 WndProc forward (implemented in imgui_impl_win32.cpp).
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace
{
struct WindowFullscreenState
{
    bool            fullscreen = false;
    LONG_PTR        saved_style = 0;
    WINDOWPLACEMENT saved_placement{};
};

bool set_borderless_fullscreen(HWND hwnd, WindowFullscreenState& st, bool enable)
{
    if (!hwnd || !IsWindow(hwnd))
        return false;

    if (enable)
    {
        if (st.fullscreen)
            return true;

        st.saved_placement.length = sizeof(WINDOWPLACEMENT);
        if (!GetWindowPlacement(hwnd, &st.saved_placement))
            return false;
        st.saved_style = GetWindowLongPtr(hwnd, GWL_STYLE);

        HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        if (!GetMonitorInfo(mon, &mi))
            return false;

        const RECT& r = mi.rcMonitor;
        const int w = r.right - r.left;
        const int h = r.bottom - r.top;

        SetWindowLongPtr(hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(hwnd, HWND_TOP, r.left, r.top, w, h,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        st.fullscreen = true;
        return true;
    }

    if (!st.fullscreen)
        return true;

    SetWindowLongPtr(hwnd, GWL_STYLE, st.saved_style | WS_VISIBLE);
    st.saved_placement.length = sizeof(WINDOWPLACEMENT);
    SetWindowPlacement(hwnd, &st.saved_placement);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    st.fullscreen = false;
    return true;
}

bool toggle_borderless_fullscreen(HWND hwnd, WindowFullscreenState& st)
{
    return set_borderless_fullscreen(hwnd, st, !st.fullscreen);
}

volatile bool g_running = true;
HWND g_hwnd = nullptr;
HINSTANCE g_hinstance = nullptr;
bool g_headless = false;
int g_headless_token = 1;
WindowFullscreenState g_fullscreen{};
AppPlatformCallbacks g_cb{};

void begin_app_exit(HWND hwnd)
{
    if (hwnd)
        ShowWindow(hwnd, SW_HIDE);
    g_running = false;
    if (g_cb.on_exit_request)
        g_cb.on_exit_request(g_cb.user);
    if (hwnd && IsWindow(hwnd))
        DestroyWindow(hwnd);
    g_hwnd = nullptr;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam);

    switch (msg)
    {
    case WM_SIZE:
        if (g_cb.on_resize)
            g_cb.on_resize(g_cb.user);
        break;

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (lParam & (1 << 30))
            break;
        if (wParam == VK_F11)
        {
            if (toggle_borderless_fullscreen(hwnd, g_fullscreen))
            {
                if (g_cb.on_resize)
                    g_cb.on_resize(g_cb.user);
            }
            return 0;
        }
        if (wParam == VK_ESCAPE)
        {
            if (g_fullscreen.fullscreen)
            {
                if (set_borderless_fullscreen(hwnd, g_fullscreen, false))
                {
                    if (g_cb.on_resize)
                        g_cb.on_resize(g_cb.user);
                }
            }
            else
            {
                begin_app_exit(hwnd);
            }
            return 0;
        }
        break;

    case WM_CLOSE:
        begin_app_exit(hwnd);
        return 0;

    case WM_DESTROY:
        g_running = false;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}
} // namespace

bool app_platform_create_window(EngineCreateInfo* create_info,
                                const char* title,
                                uint32_t width,
                                uint32_t height)
{
    if (!create_info)
        return false;

    if (create_info->headless)
    {
        const uint32_t w = width ? width : (create_info->width ? create_info->width : 1280u);
        const uint32_t h = height ? height : (create_info->height ? create_info->height : 720u);
        g_headless = true;
        g_running = true;
        g_hwnd = nullptr;
        g_hinstance = GetModuleHandle(nullptr);
        gfx_platform_configure_headless(true, w, h);
        create_info->platform_instance = g_hinstance;
        create_info->window = &g_headless_token;
        create_info->width = w;
        create_info->height = h;
        create_info->headless = true;
        std::printf("[app] headless host %ux%u (VK_EXT_headless_surface)\n", w, h);
        return true;
    }

    g_headless = false;
    gfx_platform_configure_headless(false, 0, 0);

    g_hinstance = GetModuleHandle(nullptr);
    g_running = true;
    g_fullscreen = {};

    WNDCLASS wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = g_hinstance;
    wc.lpszClassName = L"Alephium BlockFlow";
    wc.hIcon = (HICON)LoadImage(nullptr, L"resource\\Alephium-Logo-round.ico", IMAGE_ICON, 0, 0,
                                LR_LOADFROMFILE | LR_DEFAULTSIZE);
    RegisterClass(&wc);

    const int w = width ? static_cast<int>(width) : WDW_WIDTH;
    const int h = height ? static_cast<int>(height) : WDW_HEIGHT;

    // CreateWindow wants wide title; product title is UTF-8-ish ASCII.
    wchar_t wtitle[256];
    if (title && title[0])
    {
        MultiByteToWideChar(CP_UTF8, 0, title, -1, wtitle, 256);
    }
    else
    {
        wcsncpy_s(wtitle, L"Alephium BlockFlow", _TRUNCATE);
    }

    g_hwnd = CreateWindow(
        wc.lpszClassName,
        wtitle,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        w, h,
        nullptr, nullptr, g_hinstance, nullptr);

    if (!g_hwnd)
    {
        std::printf("Failed to create window\n");
        return false;
    }

    create_info->platform_instance = g_hinstance;
    create_info->window = g_hwnd;
    create_info->width = static_cast<uint32_t>(w);
    create_info->height = static_cast<uint32_t>(h);
    return true;
}

void app_platform_show_window(void* window)
{
    if (g_headless)
        return;
    HWND hwnd = static_cast<HWND>(window ? window : g_hwnd);
    if (!hwnd)
        return;
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
}

void app_platform_hide_window(void* window)
{
    if (g_headless)
        return;
    HWND hwnd = static_cast<HWND>(window ? window : g_hwnd);
    if (hwnd)
        ShowWindow(hwnd, SW_HIDE);
}

void app_platform_destroy_window(void* window)
{
    if (g_headless)
    {
        g_running = false;
        return;
    }
    HWND hwnd = static_cast<HWND>(window ? window : g_hwnd);
    if (hwnd && IsWindow(hwnd))
        DestroyWindow(hwnd);
    if (hwnd == g_hwnd)
        g_hwnd = nullptr;
}

void app_platform_set_callbacks(AppPlatformCallbacks cb)
{
    g_cb = cb;
}

int app_platform_run_loop(AppPlatformCallbacks cb)
{
    app_platform_set_callbacks(cb);
    MSG msg = {};
    while (g_running)
    {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
                g_running = false;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        Sleep(10);
    }
    return 0;
}

bool app_platform_is_running()
{
    return g_running;
}

void app_platform_request_quit()
{
    g_running = false;
}

void app_platform_poll_events()
{
    if (g_headless)
        return;
    MSG msg = {};
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
    {
        if (msg.message == WM_QUIT)
            g_running = false;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

void app_platform_sleep_ms(int ms)
{
    if (ms > 0)
        Sleep(static_cast<DWORD>(ms));
}

void app_platform_raise_window(void* window)
{
    if (g_headless)
        return;
    HWND hwnd = static_cast<HWND>(window ? window : g_hwnd);
    if (!hwnd)
        return;
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    SetForegroundWindow(hwnd);
    SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
}
