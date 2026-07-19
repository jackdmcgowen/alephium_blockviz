#pragma once

// Borderless fullscreen for the host HWND (app-owned).
// Graphics only reacts via WM_SIZE / client-rect resize — no exclusive display mode.

#include <windows.h>

struct WindowFullscreenState
{
    bool             fullscreen = false;
    LONG_PTR         saved_style = 0;
    WINDOWPLACEMENT  saved_placement{};
};

inline bool set_borderless_fullscreen(HWND hwnd, WindowFullscreenState& st, bool enable)
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

    // Exit fullscreen
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

inline bool toggle_borderless_fullscreen(HWND hwnd, WindowFullscreenState& st)
{
    return set_borderless_fullscreen(hwnd, st, !st.fullscreen);
}
