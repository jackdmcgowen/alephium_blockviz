#include "gpu_prv_lib.h"

#include <windows.h>
#include <vulkan/vulkan_win32.h>

VkSurfaceKHR create_win32_surface(VkInstance instance, void *hwnd, void *hinstance)
{
    VkSurfaceKHR surface;
    VkWin32SurfaceCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    createInfo.hwnd = (HWND)hwnd;
    createInfo.hinstance = (HINSTANCE)hinstance;

    if (vkCreateWin32SurfaceKHR(instance, &createInfo, nullptr, &surface) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create window surface");
    }

    return(surface);

}   /* create_win32_surface() */


void destroy_surface(VkInstance instance, VkSurfaceKHR surface)
{
    vkDestroySurfaceKHR(instance, surface, nullptr);

}   /* destroy_surface() */
