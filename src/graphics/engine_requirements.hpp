#pragma once

// Central Vulkan / engine requirements (E2). Keep in sync with create_device feature chain.
#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>

// App-facing engine semver (bump when public engine contract changes)
#define BLOCKVIZ_ENGINE_VERSION_MAJOR 0
#define BLOCKVIZ_ENGINE_VERSION_MINOR 2
#define BLOCKVIZ_ENGINE_VERSION_PATCH 0

inline constexpr uint32_t kRequiredVulkanApiVersion = VK_API_VERSION_1_3;

// Required logical-device features (must match create_device pNext chain in device.cpp)
struct DeviceFeatureRequirements
{
    bool timeline_semaphore = true; // Vulkan 1.2
    bool dynamic_rendering  = true; // Vulkan 1.3
    bool synchronization2   = true; // Vulkan 1.3
    bool swapchain          = true; // VK_KHR_swapchain
};

bool physical_device_meets_requirements(VkPhysicalDevice pd,
                                        const DeviceFeatureRequirements& req,
                                        char* fail_reason,
                                        size_t fail_reason_len);

void log_engine_startup(const VkPhysicalDeviceProperties& props);
