#pragma once

// OS / WSI helpers for graphics. One of gfx_platform_win32.cpp / gfx_platform_linux.cpp
// is linked per build.

#include <cstdint>
#include <vulkan/vulkan.h>

// Append platform surface extension names into caller-owned storage.
// Always includes VK_KHR_SURFACE; adds WIN32 / GLFW-required extensions as needed.
// |names| must have room for at least 4 entries. Returns count written.
uint32_t gfx_platform_surface_extension_names(const char** names, uint32_t capacity);

// Create a presentation surface from host native handles (EngineCreateInfo).
VkSurfaceKHR gfx_platform_create_surface(VkInstance instance,
                                         void* window,
                                         void* platform_instance);
void gfx_platform_destroy_surface(VkInstance instance, VkSurfaceKHR surface);

// Client / framebuffer size of the host window (content area).
void gfx_platform_get_window_size(void* window, uint32_t* out_w, uint32_t* out_h);

// ImGui platform backend (not the Vulkan renderer).
void gfx_platform_imgui_init(void* window);
void gfx_platform_imgui_shutdown();
void gfx_platform_imgui_new_frame();

// Optional OS debug sink (OutputDebugString / stderr).
void gfx_platform_debug_log(const char* msg);

// Ensure a directory exists (for validation log path, etc.).
bool gfx_platform_ensure_directory(const char* path_utf8);

// Capture host window client pixels and write PNG (implementation-defined method).
bool gfx_platform_save_window_png(void* window, const char* path_utf8);
