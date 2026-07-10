#include "gpu_prv_lib.h"
#include "engine_requirements.hpp"

#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

VkPhysicalDevice pick_physical_device(
    VkInstance instance,
    VkPhysicalDeviceProperties *device_props,
    VkPhysicalDeviceMemoryProperties *device_mem_props)
{
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (deviceCount == 0)
        throw std::runtime_error("No Vulkan-capable devices found");

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    const DeviceFeatureRequirements req{};
    VkPhysicalDevice best = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties best_props{};
    VkPhysicalDeviceMemoryProperties best_mem{};
    bool best_is_discrete = false;
    char reason[256]{};

    for (uint32_t i = 0; i < deviceCount; ++i)
    {
        VkPhysicalDeviceProperties props{};
        VkPhysicalDeviceMemoryProperties mem{};
        vkGetPhysicalDeviceProperties(devices[i], &props);
        vkGetPhysicalDeviceMemoryProperties(devices[i], &mem);

        if (!physical_device_meets_requirements(devices[i], req, reason, sizeof(reason)))
        {
            std::printf("[engine] skip device '%s': %s\n", props.deviceName, reason);
            continue;
        }

        const bool discrete = (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU);
        // Prefer discrete among devices that meet requirements; else first viable.
        if (best == VK_NULL_HANDLE || (discrete && !best_is_discrete))
        {
            best = devices[i];
            best_props = props;
            best_mem = mem;
            best_is_discrete = discrete;
        }
    }

    if (best == VK_NULL_HANDLE)
    {
        throw std::runtime_error(
            "No Vulkan device meets engine requirements "
            "(API 1.3+, timelineSemaphore, dynamicRendering, synchronization2, VK_KHR_swapchain)");
    }

    if (device_props)
        *device_props = best_props;
    if (device_mem_props)
        *device_mem_props = best_mem;

    log_engine_startup(best_props);
    return best;
}

void create_device(
    VkInstance          instance,
    VkPhysicalDevice    physicalDevice,
    VkDevice           *device,
    VkQueue            *queue )
{
    (void)instance;

    // Features must stay in sync with DeviceFeatureRequirements / physical_device_meets_requirements.
    VkDeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = 0; // Assume graphics queue at index 0
    queueCreateInfo.queueCount = 1;
    float queuePriority = 1.0f;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    VkPhysicalDeviceVulkan13Features vulkan13Features{};
    vulkan13Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    vulkan13Features.dynamicRendering = VK_TRUE;
    vulkan13Features.synchronization2 = VK_TRUE;

    VkPhysicalDeviceVulkan12Features vulkan12Features{};
    vulkan12Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    vulkan12Features.timelineSemaphore = VK_TRUE;
    vulkan12Features.pNext = &vulkan13Features;

    VkPhysicalDeviceFeatures deviceFeatures{};
    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pQueueCreateInfos = &queueCreateInfo;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pEnabledFeatures = &deviceFeatures;
    // Dynamic rendering is core in 1.3; only swapchain remains as a device extension.
    const char* extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    createInfo.enabledExtensionCount = 1;
    createInfo.ppEnabledExtensionNames = extensions;
    createInfo.pNext = &vulkan12Features;

    if (vkCreateDevice(physicalDevice, &createInfo, nullptr, device) != VK_SUCCESS)
    {
        throw std::runtime_error(
            "Failed to create logical device (features should have been validated at pick)");
    }

    vkGetDeviceQueue(*device, 0, 0, queue);
}

void destroy_device(VkDevice device)
{
    vkDestroyDevice(device, nullptr);
}

uint32_t find_device_memory_type(
    VkPhysicalDeviceMemoryProperties* deviceMemProps,
    uint32_t typeFilter,
    VkMemoryPropertyFlags properties)
{
    for (uint32_t i = 0; i < deviceMemProps->memoryTypeCount; i++)
    {
        if ((typeFilter & (1 << i)) &&
            (deviceMemProps->memoryTypes[i].propertyFlags & properties) == properties)
        {
            return i;
        }
    }
    throw std::runtime_error("Failed to find suitable memory type");
}
