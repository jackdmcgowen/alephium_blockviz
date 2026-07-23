#include "graphics/platform/gpu_platform.hpp"

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"

#include <vulkan/vulkan.h>

#include <cstdio>
#include <filesystem>
#include <stdexcept>

uint32_t gpu_platform_surface_extension_names(const char** names, uint32_t capacity)
{
    if (!names || capacity == 0)
        return 0;
    if (gpu_platform_is_headless())
    {
        if (capacity < 2)
            return 0;
        names[0] = VK_KHR_SURFACE_EXTENSION_NAME;
        names[1] = VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME;
        return 2;
    }
    uint32_t count = 0;
    uint32_t glfw_count = 0;
    const char** glfw_ext = glfwGetRequiredInstanceExtensions(&glfw_count);
    if (!glfw_ext)
    {
        if (count < capacity)
            names[count++] = VK_KHR_SURFACE_EXTENSION_NAME;
        return count;
    }
    for (uint32_t i = 0; i < glfw_count && count < capacity; ++i)
        names[count++] = glfw_ext[i];
    return count;
}

VkSurfaceKHR gpu_platform_create_surface(VkInstance instance,
                                         void* window,
                                         void* /*platform_instance*/)
{
    if (gpu_platform_is_headless())
        return gpu_platform_create_headless_surface(instance);
    if (!window)
        throw std::runtime_error("Failed to create window surface: null window");
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    const VkResult r = glfwCreateWindowSurface(
        instance, static_cast<GLFWwindow*>(window), nullptr, &surface);
    if (r != VK_SUCCESS)
        throw std::runtime_error("Failed to create window surface (glfwCreateWindowSurface)");
    return surface;
}

void gpu_platform_destroy_surface(VkInstance instance, VkSurfaceKHR surface)
{
    vkDestroySurfaceKHR(instance, surface, nullptr);
}

void gpu_platform_get_window_size(void* window, uint32_t* out_w, uint32_t* out_h)
{
    if (gpu_platform_is_headless())
    {
        if (out_w)
            *out_w = gpu_platform_headless_width();
        if (out_h)
            *out_h = gpu_platform_headless_height();
        return;
    }
    int w = 0, h = 0;
    if (window)
        glfwGetFramebufferSize(static_cast<GLFWwindow*>(window), &w, &h);
    if (out_w)
        *out_w = w > 0 ? static_cast<uint32_t>(w) : 0;
    if (out_h)
        *out_h = h > 0 ? static_cast<uint32_t>(h) : 0;
}

void gpu_platform_imgui_init(void* window)
{
    if (gpu_platform_is_headless())
        return;
    ImGui_ImplGlfw_InitForVulkan(static_cast<GLFWwindow*>(window), true);
}

void gpu_platform_imgui_shutdown()
{
    if (gpu_platform_is_headless())
        return;
    ImGui_ImplGlfw_Shutdown();
}

void gpu_platform_imgui_new_frame()
{
    if (gpu_platform_is_headless())
    {
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(static_cast<float>(gpu_platform_headless_width()),
                                static_cast<float>(gpu_platform_headless_height()));
        if (io.DeltaTime <= 0.f)
            io.DeltaTime = 1.f / 60.f;
        return;
    }
    ImGui_ImplGlfw_NewFrame();
}

void gpu_platform_debug_log(const char* msg)
{
    if (msg)
    {
        std::fputs(msg, stderr);
        std::fputc('\n', stderr);
    }
}

bool gpu_platform_ensure_directory(const char* path_utf8)
{
    if (!path_utf8 || !path_utf8[0])
        return false;
    std::error_code ec;
    std::filesystem::create_directories(path_utf8, ec);
    return !ec || std::filesystem::is_directory(path_utf8);
}

bool gpu_platform_save_window_png(void* /*window*/, const char* path_utf8)
{
    // Fallback only — preferred path is GPU swapchain readback.
    if (!path_utf8 || !path_utf8[0])
        return false;
    const int w = 4, h = 4;
    unsigned char rgba[4 * 4 * 4];
    for (int i = 0; i < w * h; ++i)
    {
        rgba[i * 4 + 0] = 255;
        rgba[i * 4 + 1] = 92;
        rgba[i * 4 + 2] = 0;
        rgba[i * 4 + 3] = 255;
    }
    std::printf("[gfx] screenshot: window-blit fallback stub on Linux: %s\n", path_utf8);
    return gpu_platform_write_png_rgba(path_utf8, w, h, rgba);
}
