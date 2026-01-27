#include "gpu_prv_lib.h"

VkPhysicalDevice pick_physical_device(VkInstance instance)
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

    std::vector<VkPhysicalDeviceProperties> deviceProps(deviceCount);

    physicalDevice = devices[0]; // Pick first device (simplified)
    for (uint32_t i = 0; i < deviceCount; ++i)
    {
        vkGetPhysicalDeviceProperties(devices[i], &deviceProps[i]);
        if (deviceProps[i].deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) //Pick discrete GPU (specific)
        {
            physicalDevice = devices[i];
            printf("%s\n", deviceProps[i].deviceName);
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

    VkPhysicalDeviceFeatures deviceFeatures{};
    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pQueueCreateInfos = &queueCreateInfo;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pEnabledFeatures = &deviceFeatures;
    const char* extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    createInfo.enabledExtensionCount = 1;
    createInfo.ppEnabledExtensionNames = extensions;

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
