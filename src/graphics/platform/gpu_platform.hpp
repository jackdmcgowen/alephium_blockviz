#pragma once

// OS / WSI helpers for graphics. One of gpu_platform_win32.cpp / gpu_platform_linux.cpp
// is linked per build. Headless helpers live in gpu_platform_common.cpp (always linked).

#include <cstdint>
#include <vulkan/vulkan.h>

// --- Headless (VK_EXT_headless_surface) — shared ---
// Call before instance/surface create (GraphicsSystem::configure).
void gpu_platform_configure_headless(bool enabled, uint32_t width, uint32_t height);
bool gpu_platform_is_headless();
uint32_t gpu_platform_headless_width();
uint32_t gpu_platform_headless_height();

// Shared: create VK_EXT_headless_surface (used by platform create when headless).
VkSurfaceKHR gpu_platform_create_headless_surface(VkInstance instance);

// Append platform surface extension names into caller-owned storage.
// Always includes VK_KHR_SURFACE; adds WIN32 / GLFW / HEADLESS as needed.
// |names| must have room for at least 4 entries. Returns count written.
uint32_t gpu_platform_surface_extension_names(const char** names, uint32_t capacity);

// Create a presentation surface from host native handles (EngineCreateInfo).
// Headless: ignores window handles, uses vkCreateHeadlessSurfaceEXT.
VkSurfaceKHR gpu_platform_create_surface(VkInstance instance,
                                         void* window,
                                         void* platform_instance);
void gpu_platform_destroy_surface(VkInstance instance, VkSurfaceKHR surface);

// Client / framebuffer size of the host window (content area).
// Headless: returns configured headless extent.
void gpu_platform_get_window_size(void* window, uint32_t* out_w, uint32_t* out_h);

// ImGui platform backend (not the Vulkan renderer).
// Headless: no-op init; NewFrame sets DisplaySize only.
void gpu_platform_imgui_init(void* window);
void gpu_platform_imgui_shutdown();
void gpu_platform_imgui_new_frame();

// Optional OS debug sink (OutputDebugString / stderr).
void gpu_platform_debug_log(const char* msg);

// Ensure a directory exists (for validation log path, etc.).
bool gpu_platform_ensure_directory(const char* path_utf8);

// Capture host window client pixels and write PNG (window blit path).
// Prefer GraphicsSystem GPU readback when available; this is fallback.
bool gpu_platform_save_window_png(void* window, const char* path_utf8);

// Write RGBA8 PNG (shared; used by GPU readback and Linux stub).
bool gpu_platform_write_png_rgba(const char* path_utf8, int w, int h, const unsigned char* rgba);
