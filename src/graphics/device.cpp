#include "gpu_prv_lib.h"

VkPhysicalDevice pick_physical_device(
    VkInstance instance,
    VkPhysicalDeviceProperties *device_props,
    VkPhysicalDeviceMemoryProperties *device_mem_props)
{
    VkPhysicalDevice physicalDevice;
    uint32_t deviceCount = 0;

    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (deviceCount == 0)
    {
        throw std::runtime_error("No Vulkan-capable devices found");
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    VkPhysicalDeviceProperties2 deviceProps2{};
    deviceProps2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;

    physicalDevice = devices[0]; // Pick first device (simplified)
    for (uint32_t i = 0; i < deviceCount; ++i)
    {
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceProperties(devices[i], device_props);
        vkGetPhysicalDeviceMemoryProperties(devices[i], device_mem_props);
        vkGetPhysicalDeviceProperties2(devices[i], &deviceProps2);
        vkGetPhysicalDeviceQueueFamilyProperties2(devices[i], &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties2> queueFamilyProps(queueFamilyCount);

        for (auto &queueFamilyProp : queueFamilyProps)
        {
            queueFamilyProp.sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
        }
        vkGetPhysicalDeviceQueueFamilyProperties2(devices[i], &queueFamilyCount, queueFamilyProps.data());


        if (device_props->deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) //Pick discrete GPU (specific)
        {
            physicalDevice = devices[i];
            printf("%s\n", device_props->deviceName);
            break;
        }
    }

    return(physicalDevice);

}   /* pick_physical_device() */


void create_device(
    VkInstance          instance, 
    VkPhysicalDevice    physicalDevice,
    VkDevice           *device,
    VkQueue            *queue )
{
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
    const char* extensions[] = 
        { 
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME
        };
    createInfo.enabledExtensionCount = 2;
    createInfo.ppEnabledExtensionNames = extensions;
    createInfo.pNext = &vulkan12Features;

    if (vkCreateDevice(physicalDevice, &createInfo, nullptr, device) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create logical device");
    }

    vkGetDeviceQueue(*device, 0, 0, queue);

}   /* create_device() */


void destroy_device(VkDevice device)
{
    vkDestroyDevice(device, nullptr);

}   /* destroy_device() */


uint32_t find_device_memory_type(
    VkPhysicalDeviceMemoryProperties* deviceMemProps,
    uint32_t typeFilter,
    VkMemoryPropertyFlags properties)
{

    for (uint32_t i = 0; i < deviceMemProps->memoryTypeCount; i++)
    {
        if ((typeFilter & (1 << i)) && (deviceMemProps->memoryTypes[i].propertyFlags & properties) == properties)
        {
            return i;
        }
    }
    throw std::runtime_error("Failed to find suitable memory type");

}   /* find_device_memory_type() */
