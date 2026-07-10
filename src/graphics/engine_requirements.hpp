#pragma once

// Central Vulkan / engine requirements (E2). Keep in sync with create_device feature chain.
// App/engine product names and versions live outside this TU (host / engine_identity).
#include "gpu_pub_lib.h"

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>

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

// Logs selected GPU + engine product identity supplied by the engine layer.
void log_engine_startup(const VkPhysicalDeviceProperties& props,
                        const SoftwareIdentity& engine);
