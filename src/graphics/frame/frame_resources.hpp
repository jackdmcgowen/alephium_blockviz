#pragma once

// GPU mesh/instance/UBO buffers for the cube path (E8 / G1 BufferManager).
// Also holds GPU frustum-cull outputs (compact instances + indirect draw args).
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

    // Reset indirect instanceCount=0 and indexCount before GPU cull (host-visible).
    void reset_cull_draw_args(uint32_t index_count = 36);

    VkBuffer vertex_buffer() const { return vertex_.handle(); }
    VkBuffer index_buffer() const { return index_.handle(); }
    VkBuffer instance_buffer() const { return instance_.handle(); }
    // Post-cull compact instances (vertex input for DrawIndexedIndirect).
    VkBuffer visible_instance_buffer() const { return visible_instances_.handle(); }
    VkBuffer cull_draw_args_buffer() const { return cull_draw_args_.handle(); }
    VkBuffer outline_instance_buffer() const { return outline_.handle(); }
    VkBuffer uniform_buffer() const { return uniform_.handle(); }
    size_t instance_count() const { return instance_count_; }
    size_t outline_count() const { return outline_count_; }
    uint32_t max_instances() const { return max_instances_; }
    bool cull_buffers_ready() const
    {
        return visible_instances_.valid() && cull_draw_args_.valid();
    }

private:
    BufferManager* buffers_ = nullptr;
    GpuBuffer vertex_;
    GpuBuffer index_;
    GpuBuffer instance_;          // host upload + SSBO in for cull
    GpuBuffer visible_instances_; // SSBO out + vertex for draw
    GpuBuffer cull_draw_args_;    // VkDrawIndexedIndirectCommand (host + SSBO)
    GpuBuffer outline_;           // Sobel outline InstanceData[]
    GpuBuffer uniform_;
    void* mapped_instances_ = nullptr;
    void* mapped_outline_ = nullptr;
    void* mapped_draw_args_ = nullptr;
    size_t instance_count_ = 0;
    size_t outline_count_ = 0;
    uint32_t max_instances_ = 0;
};
