#include "graphics/pch.h"
#include "gpu_prv_lib.h"
#include "graphics/platform/gfx_platform.hpp"

// Thin wrappers — implementation lives in graphics/platform/gfx_platform_*.cpp

VkSurfaceKHR create_platform_surface(VkInstance instance, void* window, void* platform_instance)
{
    return gfx_platform_create_surface(instance, window, platform_instance);
}

void destroy_surface(VkInstance instance, VkSurfaceKHR surface)
{
    gfx_platform_destroy_surface(instance, surface);
}
