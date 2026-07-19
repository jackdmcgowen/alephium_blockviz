#pragma once

// GPU mesh/instance/UBO buffers for the cube path (E8 / G1 BufferManager).
#include "graphics/frame/vertex_types.hpp"
#include "graphics/buffer_manager.hpp"
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
    BufferManager* buffer_manager = nullptr; // required (G1)
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

    size_t upload_instances(const GpuInstance* instances, size_t count);
    // Compact outline cubes for single-pass Sobel (pos/scale/color).
    size_t upload_outline_instances(const InstanceData* instances, size_t count);
    void upload_camera(const CameraUBO& camera, glm::mat4* out_view_proj = nullptr);

    VkBuffer vertex_buffer() const { return vertex_.handle(); }
    VkBuffer index_buffer() const { return index_.handle(); }
    VkBuffer instance_buffer() const { return instance_.handle(); }
    VkBuffer outline_instance_buffer() const { return outline_.handle(); }
    VkBuffer uniform_buffer() const { return uniform_.handle(); }
    size_t instance_count() const { return instance_count_; }
    size_t outline_count() const { return outline_count_; }
    uint32_t max_instances() const { return max_instances_; }

private:
    BufferManager* buffers_ = nullptr;
    GpuBuffer vertex_;
    GpuBuffer index_;
    GpuBuffer instance_;
    GpuBuffer outline_; // Sobel outline InstanceData[]
    GpuBuffer uniform_;
    void* mapped_instances_ = nullptr;
    void* mapped_outline_ = nullptr;
    size_t instance_count_ = 0;
    size_t outline_count_ = 0;
    uint32_t max_instances_ = 0;
};
