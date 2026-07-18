#include "graphics/pch.h"
#include "gpu_prv_lib.h"
#include "engine_requirements.hpp"

#include <windows.h>
#include <vulkan/vulkan_win32.h>

VkInstance create_instance(const SoftwareIdentity& application,
                           const SoftwareIdentity& engine)
{
    VkInstance instance;

    const char* app_name = (application.name && application.name[0])
                               ? application.name
                               : "App";
    const char* eng_name = (engine.name && engine.name[0])
                               ? engine.name
                               : "Engine";

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = app_name;
    appInfo.applicationVersion = VK_MAKE_VERSION(
        application.version_major,
        application.version_minor,
        application.version_patch);
    appInfo.pEngineName = eng_name;
    appInfo.engineVersion = VK_MAKE_VERSION(
        engine.version_major,
        engine.version_minor,
        engine.version_patch);
    appInfo.apiVersion = kRequiredVulkanApiVersion;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    const char* extensions[] =
    {
#ifndef NDEBUG
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
#endif

        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
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

    return instance;

}   /* create_instance() */


void destroy_instance(VkInstance instance)
{
    vkDestroyInstance(instance, nullptr);

}   /* destroy_instance() */
