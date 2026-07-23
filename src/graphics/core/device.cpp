#include "graphics/pch.h"
#include "gpu_prv_lib.h"
#include "engine_requirements.hpp"
#include "graphics/core/queue_types.hpp"

#include <algorithm>
#include <cstdio>
#include <stdexcept>
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

    return best;
}

static void select_queue_families(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
                                  DeviceQueues& out)
{
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, nullptr);
    std::vector<VkQueueFamilyProperties> props(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, props.data());

    uint32_t fam_3d = UINT32_MAX;
    uint32_t fam_cmp = UINT32_MAX;
    uint32_t fam_tx = UINT32_MAX;
    uint32_t fam_cmp_fallback = UINT32_MAX;
    uint32_t fam_tx_fallback = UINT32_MAX;

    for (uint32_t i = 0; i < count; ++i)
    {
        const VkQueueFlags flags = props[i].queueFlags;
        const bool graphics = (flags & VK_QUEUE_GRAPHICS_BIT) != 0;
        const bool compute  = (flags & VK_QUEUE_COMPUTE_BIT) != 0;
        const bool transfer = (flags & VK_QUEUE_TRANSFER_BIT) != 0 || graphics || compute;

        VkBool32 present = VK_FALSE;
        if (surface != VK_NULL_HANDLE)
            vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface, &present);

        // _3D: graphics + present
        if (graphics && present && fam_3d == UINT32_MAX)
            fam_3d = i;

        // Prefer dedicated compute (compute without graphics)
        if (compute && !graphics && fam_cmp == UINT32_MAX)
            fam_cmp = i;
        if (compute && fam_cmp_fallback == UINT32_MAX)
            fam_cmp_fallback = i;

        // Prefer dedicated transfer (transfer bit, no graphics/compute)
        if ((flags & VK_QUEUE_TRANSFER_BIT) && !graphics && !compute && fam_tx == UINT32_MAX)
            fam_tx = i;
        if (transfer && fam_tx_fallback == UINT32_MAX)
            fam_tx_fallback = i;
    }

    if (fam_3d == UINT32_MAX)
    {
        // Fall back: any graphics family (present may still work on many Win32 drivers)
        for (uint32_t i = 0; i < count; ++i)
        {
            if (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                fam_3d = i;
                break;
            }
        }
    }
    if (fam_3d == UINT32_MAX)
        throw std::runtime_error("No graphics queue family found");

    if (fam_cmp == UINT32_MAX)
        fam_cmp = (fam_cmp_fallback != UINT32_MAX) ? fam_cmp_fallback : fam_3d;
    if (fam_tx == UINT32_MAX)
        fam_tx = (fam_tx_fallback != UINT32_MAX) ? fam_tx_fallback : fam_3d;

    out.family[static_cast<uint32_t>(QueueType::_3D)] = fam_3d;
    out.family[static_cast<uint32_t>(QueueType::TX)]  = fam_tx;
    out.family[static_cast<uint32_t>(QueueType::CMP)] = fam_cmp;
    out.dedicated_tx  = (fam_tx != fam_3d);
    out.dedicated_cmp = (fam_cmp != fam_3d);

    std::printf("[engine] queues: _3D family=%u  TX family=%u%s  CMP family=%u%s\n",
                fam_3d,
                fam_tx, out.dedicated_tx ? " (dedicated)" : " (shared)",
                fam_cmp, out.dedicated_cmp ? " (dedicated)" : " (shared)");
}

void create_device(
    VkInstance          instance,
    VkPhysicalDevice    physicalDevice,
    VkSurfaceKHR        surface,
    VkDevice           *device,
    DeviceQueues       *out_queues)
{
    (void)instance;
    if (!device || !out_queues)
        throw std::runtime_error("create_device: null out params");

    DeviceQueues qinfo{};
    select_queue_families(physicalDevice, surface, qinfo);

    // Unique families for VkDeviceQueueCreateInfo
    uint32_t unique_families[3]{};
    uint32_t unique_count = 0;
    auto add_unique = [&](uint32_t fam) {
        for (uint32_t i = 0; i < unique_count; ++i)
            if (unique_families[i] == fam)
                return;
        unique_families[unique_count++] = fam;
    };
    add_unique(qinfo.family_index(QueueType::_3D));
    add_unique(qinfo.family_index(QueueType::TX));
    add_unique(qinfo.family_index(QueueType::CMP));

    float priority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queue_cis(unique_count);
    for (uint32_t i = 0; i < unique_count; ++i)
    {
        queue_cis[i] = {};
        queue_cis[i].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_cis[i].queueFamilyIndex = unique_families[i];
        queue_cis[i].queueCount = 1;
        queue_cis[i].pQueuePriorities = &priority;
    }

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
    createInfo.pQueueCreateInfos = queue_cis.data();
    createInfo.queueCreateInfoCount = unique_count;
    createInfo.pEnabledFeatures = &deviceFeatures;
    const char* extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    createInfo.enabledExtensionCount = 1;
    createInfo.ppEnabledExtensionNames = extensions;
    createInfo.pNext = &vulkan12Features;

    if (vkCreateDevice(physicalDevice, &createInfo, nullptr, device) != VK_SUCCESS)
    {
        throw std::runtime_error(
            "Failed to create logical device (features should have been validated at pick)");
    }

    for (uint32_t i = 0; i < queue_type_count(); ++i)
    {
        const QueueType t = static_cast<QueueType>(i);
        vkGetDeviceQueue(*device, qinfo.family_index(t), 0, &qinfo.handle[i]);
    }

    *out_queues = qinfo;
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
