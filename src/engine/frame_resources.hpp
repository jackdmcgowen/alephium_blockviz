#pragma once

// GPU mesh/instance/UBO buffers for the cube path (E8).
#include "engine/vertex_types.hpp"
#include "graphics/gpu_pub_lib.h"

#define GLM_FORCE_LEFT_HANDED
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>

struct FrameResourcesCreateInfo
{
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties* mem_props = nullptr;
    const VertexNormal* cube_vertices = nullptr;
    size_t cube_vertex_bytes = 0;
    const uint16_t* cube_indices = nullptr;
    size_t cube_index_bytes = 0;
    uint32_t max_instances = 0;
};

class FrameResources
{
public:
    void create(const FrameResourcesCreateInfo& info);
    void destroy(VkDevice device);

    // Upload instance list (pos+color). Returns bound instance count.
    size_t upload_instances(const GpuInstance* instances, size_t count);
    void upload_camera(const CameraUBO& camera, glm::mat4* out_view_proj = nullptr);

    VkBuffer vertex_buffer() const { return vertex_buffer_; }
    VkBuffer index_buffer() const { return index_buffer_; }
    VkBuffer instance_buffer() const { return instance_buffer_; }
    VkBuffer uniform_buffer() const { return uniform_buffer_; }
    size_t instance_count() const { return instance_count_; }
    uint32_t max_instances() const { return max_instances_; }

    // For descriptor write after create
    VkBuffer ubo_buffer_handle() const { return uniform_buffer_; }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties* mem_props_ = nullptr;

    VkBuffer vertex_buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory vertex_memory_ = VK_NULL_HANDLE;
    VkBuffer index_buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory index_memory_ = VK_NULL_HANDLE;
    VkBuffer instance_buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory instance_memory_ = VK_NULL_HANDLE;
    void* mapped_instances_ = nullptr;
    VkBuffer uniform_buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory uniform_memory_ = VK_NULL_HANDLE;

    size_t instance_count_ = 0;
    uint32_t max_instances_ = 0;
};
