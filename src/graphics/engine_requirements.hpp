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

// Optional capabilities (not required for boot). Queried after device pick.
// Mesh path is only used when extension + meshShader feature are both true.
struct DeviceOptionalFeatures
{
    bool mesh_shader_ext = false; // VK_EXT_mesh_shader present
    bool mesh_shader     = false; // VkPhysicalDeviceMeshShaderFeaturesEXT::meshShader
    bool task_shader     = false; // ::taskShader
    // Convenience: product path may use mesh when both ext + meshShader (task optional).
    bool mesh_path_usable() const { return mesh_shader_ext && mesh_shader; }

    uint32_t max_mesh_output_vertices   = 0;
    uint32_t max_mesh_output_primitives = 0;
    uint32_t max_mesh_work_group_invocations = 0;
};

bool physical_device_meets_requirements(VkPhysicalDevice pd,
                                        const DeviceFeatureRequirements& req,
                                        char* fail_reason,
                                        size_t fail_reason_len);

// Query optional features/properties (does not enable anything on the logical device).
void query_optional_device_features(VkPhysicalDevice pd, DeviceOptionalFeatures& out);

// Logs selected GPU + engine product identity supplied by the engine layer.
void log_engine_startup(const VkPhysicalDeviceProperties& props,
                        const SoftwareIdentity& engine);

void log_optional_device_features(const DeviceOptionalFeatures& opt);
