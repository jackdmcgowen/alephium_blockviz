#pragma once

// Host window + event loop. One of app_platform_win32.cpp / app_platform_linux.cpp
// is linked per build. Product main stays free of OS window APIs.

#include "graphics/gpu_pub_lib.h"

#include <cstdint>

#ifndef WDW_WIDTH
#define WDW_WIDTH  1024
#endif
#ifndef WDW_HEIGHT
#define WDW_HEIGHT 1024
#endif

struct AppPlatformCallbacks
{
    // Called on client-area resize (swapchain recreate).
    void (*on_resize)(void* user) = nullptr;
    // Hide window, stop systems while native handle still valid, then destroy.
    void (*on_exit_request)(void* user) = nullptr;
    void* user = nullptr;
};

// Create host window; fills create_info.window / platform_instance / width / height.
// Returns false on failure.
bool app_platform_create_window(EngineCreateInfo* create_info,
                                const char* title,
                                uint32_t width,
                                uint32_t height);

// Show the window after engine systems are started.
void app_platform_show_window(void* window);

// Hide native window (responsive close).
void app_platform_hide_window(void* window);

// Destroy native window if still alive.
void app_platform_destroy_window(void* window);

// Install callbacks used by poll/run (resize, exit). Safe before or after create.
void app_platform_set_callbacks(AppPlatformCallbacks cb);

// Run the OS message / event loop until quit. Invokes callbacks for resize/exit.
// Returns process exit code (0 = ok).
int app_platform_run_loop(AppPlatformCallbacks cb);

// True while the loop should keep pumping (cleared on quit / destroy).
bool app_platform_is_running();
void app_platform_request_quit();

// Non-blocking event pump (harnesses / warmup waits). Does not block until quit.
void app_platform_poll_events();

// Sleep approximately |ms| milliseconds (may be interrupted by OS).
void app_platform_sleep_ms(int ms);

// Optional: raise window for capture (no-op if unsupported).
void app_platform_raise_window(void* window);
