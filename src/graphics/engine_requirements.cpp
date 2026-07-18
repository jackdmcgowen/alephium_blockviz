#include "graphics/pch.h"
#include "engine_requirements.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static bool device_has_extension(VkPhysicalDevice pd, const char* name)
{
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(pd, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> exts(count);
    if (count > 0)
        vkEnumerateDeviceExtensionProperties(pd, nullptr, &count, exts.data());

    for (const VkExtensionProperties& e : exts)
    {
        if (std::strcmp(e.extensionName, name) == 0)
            return true;
    }
    return false;
}

bool physical_device_meets_requirements(VkPhysicalDevice pd,
                                        const DeviceFeatureRequirements& req,
                                        char* fail_reason,
                                        size_t fail_reason_len)
{
    auto set_fail = [&](const char* msg) {
        if (fail_reason && fail_reason_len > 0)
        {
            std::snprintf(fail_reason, fail_reason_len, "%s", msg);
            fail_reason[fail_reason_len - 1] = '\0';
        }
    };

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(pd, &props);

    if (props.apiVersion < kRequiredVulkanApiVersion)
    {
        set_fail("Vulkan API version below 1.3");
        return false;
    }

    VkPhysicalDeviceVulkan13Features f13{};
    f13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;

    VkPhysicalDeviceVulkan12Features f12{};
    f12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    f12.pNext = &f13;

    VkPhysicalDeviceFeatures2 features2{};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features2.pNext = &f12;
    vkGetPhysicalDeviceFeatures2(pd, &features2);

    if (req.timeline_semaphore && !f12.timelineSemaphore)
    {
        set_fail("missing timelineSemaphore (Vulkan 1.2)");
        return false;
    }
    if (req.dynamic_rendering && !f13.dynamicRendering)
    {
        set_fail("missing dynamicRendering (Vulkan 1.3)");
        return false;
    }
    if (req.synchronization2 && !f13.synchronization2)
    {
        set_fail("missing synchronization2 (Vulkan 1.3)");
        return false;
    }
    if (req.swapchain && !device_has_extension(pd, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
    {
        set_fail("missing VK_KHR_swapchain");
        return false;
    }

    if (fail_reason && fail_reason_len > 0)
        fail_reason[0] = '\0';
    return true;
}

void log_engine_startup(const VkPhysicalDeviceProperties& props,
                        const SoftwareIdentity& engine)
{
    const char* eng_name = (engine.name && engine.name[0]) ? engine.name : "Engine";
    std::printf("[engine] %s %u.%u.%u\n",
                eng_name,
                engine.version_major,
                engine.version_minor,
                engine.version_patch);
    std::printf("[engine] device: %s\n", props.deviceName);
    std::printf("[engine] apiVersion: %u.%u.%u  driverVersion: %u\n",
                VK_VERSION_MAJOR(props.apiVersion),
                VK_VERSION_MINOR(props.apiVersion),
                VK_VERSION_PATCH(props.apiVersion),
                props.driverVersion);
    std::printf("[engine] required: timelineSemaphore, dynamicRendering, synchronization2, swapchain\n");
}
