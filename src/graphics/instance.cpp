#include "gpu_prv_lib.h"

#include <windows.h>
#include <vulkan/vulkan_win32.h>

VkInstance create_instance()
{
    VkInstance   instance;
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Alephium DAG";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    const char* extensions[] =
    {
#ifndef NDEBUG
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
#endif
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME
    };
    const char* enbl_layers[] = { "VK_LAYER_KHRONOS_validation" };
#ifndef NDEBUG
    createInfo.enabledExtensionCount = 3;
#else
    createInfo.enabledExtensionCount = 2;
#endif
    createInfo.ppEnabledExtensionNames = extensions;
#ifndef NDEBUG
    createInfo.enabledLayerCount = 1;
    createInfo.ppEnabledLayerNames = enbl_layers;
#endif

    if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create Vulkan instance");
    }

    return(instance);

}   /* create_instance() */


void destroy_instance(VkInstance instance)
{
vkDestroyInstance(instance, nullptr);

}   /* destroy_instance() */

