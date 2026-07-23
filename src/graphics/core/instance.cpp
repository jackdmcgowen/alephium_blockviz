#include "graphics/pch.h"
#include "gpu_prv_lib.h"
#include "engine_requirements.hpp"
#include "graphics/platform/gpu_platform.hpp"

#include <cstring>
#include <vector>

namespace
{
bool instance_layer_available(const char* name)
{
    uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layers(count);
    if (count)
        vkEnumerateInstanceLayerProperties(&count, layers.data());
    for (const auto& l : layers)
    {
        if (std::strcmp(l.layerName, name) == 0)
            return true;
    }
    return false;
}

bool instance_extension_available(const char* name)
{
    uint32_t count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> exts(count);
    if (count)
        vkEnumerateInstanceExtensionProperties(nullptr, &count, exts.data());
    for (const auto& e : exts)
    {
        if (std::strcmp(e.extensionName, name) == 0)
            return true;
    }
    return false;
}
} // namespace

VkInstance create_instance(const SoftwareIdentity& application,
                           const SoftwareIdentity& engine,
                           bool enable_validation)
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

    const char* ext_storage[8] = {};
    uint32_t ext_count = 0;
#ifndef NDEBUG
    if (enable_validation && instance_extension_available(VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
        ext_storage[ext_count++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
#else
    (void)enable_validation;
#endif
    {
        const char* surf[4] = {};
        const uint32_t n = gpu_platform_surface_extension_names(surf, 4);
        for (uint32_t i = 0; i < n && ext_count < 8; ++i)
        {
            if (instance_extension_available(surf[i]))
                ext_storage[ext_count++] = surf[i];
            else
                std::printf("[gfx] warning: instance extension missing: %s\n", surf[i]);
        }
    }

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = ext_count;
    createInfo.ppEnabledExtensionNames = ext_storage;

    const char* enbl_layers[] = { "VK_LAYER_KHRONOS_validation" };
#ifndef NDEBUG
    if (enable_validation && instance_layer_available(enbl_layers[0]))
    {
        createInfo.enabledLayerCount = 1;
        createInfo.ppEnabledLayerNames = enbl_layers;
    }
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
