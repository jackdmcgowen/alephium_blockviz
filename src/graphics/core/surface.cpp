#include "graphics/pch.h"
#include "gpu_prv_lib.h"
#include "graphics/platform/gpu_platform.hpp"

// Thin wrappers — implementation lives in graphics/platform/gpu_platform_*.cpp

VkSurfaceKHR create_platform_surface(VkInstance instance, void* window, void* platform_instance)
{
    return gpu_platform_create_surface(instance, window, platform_instance);
}

void destroy_surface(VkInstance instance, VkSurfaceKHR surface)
{
    gpu_platform_destroy_surface(instance, surface);
}
