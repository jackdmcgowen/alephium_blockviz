#include "app/platform/app_platform.hpp"

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstring>

namespace
{
volatile bool g_running = true;
GLFWwindow* g_window = nullptr;
bool g_fullscreen = false;
int g_win_x = 100, g_win_y = 100, g_win_w = WDW_WIDTH, g_win_h = WDW_HEIGHT;
AppPlatformCallbacks g_cb{};

void begin_app_exit(GLFWwindow* window)
{
    if (window)
        glfwHideWindow(window);
    g_running = false;
    if (g_cb.on_exit_request)
        g_cb.on_exit_request(g_cb.user);
    // Defer destroy to after engine stop; run_loop will destroy if still set.
}

void framebuffer_size_cb(GLFWwindow* /*window*/, int /*w*/, int /*h*/)
{
    if (g_cb.on_resize)
        g_cb.on_resize(g_cb.user);
}

void key_cb(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/)
{
    if (action != GLFW_PRESS && action != GLFW_REPEAT)
        return;
    // Ignore auto-repeat for F11 / Esc edge actions.
    if (action == GLFW_REPEAT && (key == GLFW_KEY_F11 || key == GLFW_KEY_ESCAPE))
        return;

    if (key == GLFW_KEY_F11 && action == GLFW_PRESS)
    {
        if (!g_fullscreen)
        {
            glfwGetWindowPos(window, &g_win_x, &g_win_y);
            glfwGetWindowSize(window, &g_win_w, &g_win_h);
            GLFWmonitor* mon = glfwGetPrimaryMonitor();
            const GLFWvidmode* mode = mon ? glfwGetVideoMode(mon) : nullptr;
            if (mon && mode)
            {
                glfwSetWindowMonitor(window, mon, 0, 0, mode->width, mode->height,
                                     mode->refreshRate);
                g_fullscreen = true;
                if (g_cb.on_resize)
                    g_cb.on_resize(g_cb.user);
            }
        }
        else
        {
            glfwSetWindowMonitor(window, nullptr, g_win_x, g_win_y, g_win_w, g_win_h, 0);
            g_fullscreen = false;
            if (g_cb.on_resize)
                g_cb.on_resize(g_cb.user);
        }
        return;
    }

    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
    {
        if (g_fullscreen)
        {
            glfwSetWindowMonitor(window, nullptr, g_win_x, g_win_y, g_win_w, g_win_h, 0);
            g_fullscreen = false;
            if (g_cb.on_resize)
                g_cb.on_resize(g_cb.user);
        }
        else
        {
            begin_app_exit(window);
        }
    }
}

void window_close_cb(GLFWwindow* window)
{
    // Don't destroy yet; match Windows: hide + stop systems while handle valid.
    glfwSetWindowShouldClose(window, GLFW_FALSE);
    begin_app_exit(window);
}
} // namespace

bool app_platform_create_window(EngineCreateInfo* create_info,
                                const char* title,
                                uint32_t width,
                                uint32_t height)
{
    if (!create_info)
        return false;

    if (!glfwInit())
    {
        std::printf("Failed to initialize GLFW\n");
        return false;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    // Harnesses show after engine start; product calls app_platform_show_window.
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    const int w = width ? static_cast<int>(width) : WDW_WIDTH;
    const int h = height ? static_cast<int>(height) : WDW_HEIGHT;
    g_win_w = w;
    g_win_h = h;

    const char* t = (title && title[0]) ? title : "Alephium BlockFlow";
    g_window = glfwCreateWindow(w, h, t, nullptr, nullptr);
    if (!g_window)
    {
        std::printf("Failed to create window\n");
        glfwTerminate();
        return false;
    }

    g_running = true;
    g_fullscreen = false;
    glfwSetFramebufferSizeCallback(g_window, framebuffer_size_cb);
    glfwSetKeyCallback(g_window, key_cb);
    glfwSetWindowCloseCallback(g_window, window_close_cb);

    // platform_instance unused on GLFW/Linux (Win32 HINSTANCE).
    create_info->platform_instance = nullptr;
    create_info->window = g_window;
    create_info->width = static_cast<uint32_t>(w);
    create_info->height = static_cast<uint32_t>(h);
    return true;
}

void app_platform_show_window(void* window)
{
    GLFWwindow* w = static_cast<GLFWwindow*>(window ? window : g_window);
    if (w)
        glfwShowWindow(w);
}

void app_platform_hide_window(void* window)
{
    GLFWwindow* w = static_cast<GLFWwindow*>(window ? window : g_window);
    if (w)
        glfwHideWindow(w);
}

void app_platform_destroy_window(void* window)
{
    GLFWwindow* w = static_cast<GLFWwindow*>(window ? window : g_window);
    if (w)
    {
        glfwDestroyWindow(w);
        if (w == g_window)
            g_window = nullptr;
    }
}

void app_platform_set_callbacks(AppPlatformCallbacks cb)
{
    g_cb = cb;
}

int app_platform_run_loop(AppPlatformCallbacks cb)
{
    app_platform_set_callbacks(cb);
    while (g_running && g_window)
    {
        glfwPollEvents();
        if (glfwWindowShouldClose(g_window))
            begin_app_exit(g_window);
        // Light sleep equivalent: poll is fine; avoid busy spin with tiny wait.
        glfwWaitEventsTimeout(0.01);
    }

    if (g_window)
    {
        glfwDestroyWindow(g_window);
        g_window = nullptr;
    }
    glfwTerminate();
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
    if (!g_window)
        return;
    glfwPollEvents();
    if (glfwWindowShouldClose(g_window))
        begin_app_exit(g_window);
}

void app_platform_sleep_ms(int ms)
{
    if (ms <= 0)
        return;
    // Sub-sleeps so harnesses remain responsive to close events.
    const int slice = 10;
    int left = ms;
    while (left > 0 && g_running)
    {
        const int step = left < slice ? left : slice;
        glfwWaitEventsTimeout(static_cast<double>(step) / 1000.0);
        left -= step;
        if (g_window && glfwWindowShouldClose(g_window))
            begin_app_exit(g_window);
    }
}

void app_platform_raise_window(void* window)
{
    GLFWwindow* w = static_cast<GLFWwindow*>(window ? window : g_window);
    if (w)
        glfwFocusWindow(w);
}
