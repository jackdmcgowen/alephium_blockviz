#include "engine/frame_resources.hpp"
#include "gpu_prv_lib.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

void FrameResources::create(const FrameResourcesCreateInfo& info)
{
    if (!info.device || !info.mem_props || !info.cube_vertices || !info.cube_indices ||
        info.max_instances == 0)
        throw std::runtime_error("FrameResources::create: invalid info");

    destroy(info.device);
    device_ = info.device;
    mem_props_ = info.mem_props;
    max_instances_ = info.max_instances;

    create_buffer(device_, mem_props_, info.cube_vertex_bytes,
                  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  vertex_buffer_, vertex_memory_);
    {
        void* data = nullptr;
        vkMapMemory(device_, vertex_memory_, 0, info.cube_vertex_bytes, 0, &data);
        if (data)
        {
            std::memcpy(data, info.cube_vertices, info.cube_vertex_bytes);
            vkUnmapMemory(device_, vertex_memory_);
        }
    }

    create_buffer(device_, mem_props_, info.cube_index_bytes,
                  VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  index_buffer_, index_memory_);
    {
        void* data = nullptr;
        vkMapMemory(device_, index_memory_, 0, info.cube_index_bytes, 0, &data);
        if (data)
        {
            std::memcpy(data, info.cube_indices, info.cube_index_bytes);
            vkUnmapMemory(device_, index_memory_);
        }
    }

    const VkDeviceSize inst_size =
        static_cast<VkDeviceSize>(sizeof(InstanceData)) * max_instances_;
    create_buffer(device_, mem_props_, inst_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  instance_buffer_, instance_memory_);
    vkMapMemory(device_, instance_memory_, 0, inst_size, 0, &mapped_instances_);
    instance_count_ = 0;

    create_buffer(device_, mem_props_, sizeof(UniformBufferObject),
                  VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  uniform_buffer_, uniform_memory_);
}

void FrameResources::destroy(VkDevice device)
{
    if (!device)
        return;

    if (mapped_instances_)
    {
        vkUnmapMemory(device, instance_memory_);
        mapped_instances_ = nullptr;
    }
    if (vertex_buffer_ != VK_NULL_HANDLE)
        destroy_buffer(device, vertex_buffer_, vertex_memory_);
    if (index_buffer_ != VK_NULL_HANDLE)
        destroy_buffer(device, index_buffer_, index_memory_);
    if (instance_buffer_ != VK_NULL_HANDLE)
        destroy_buffer(device, instance_buffer_, instance_memory_);
    if (uniform_buffer_ != VK_NULL_HANDLE)
        destroy_buffer(device, uniform_buffer_, uniform_memory_);

    vertex_buffer_ = index_buffer_ = instance_buffer_ = uniform_buffer_ = VK_NULL_HANDLE;
    vertex_memory_ = index_memory_ = instance_memory_ = uniform_memory_ = VK_NULL_HANDLE;
    instance_count_ = 0;
    max_instances_ = 0;
    device_ = VK_NULL_HANDLE;
    mem_props_ = nullptr;
}

size_t FrameResources::upload_instances(const GpuInstance* instances, size_t count)
{
    instance_count_ = 0;
    if (!mapped_instances_ || !instances || count == 0)
        return 0;

    static_assert(sizeof(GpuInstance) == sizeof(InstanceData),
                  "GpuInstance/InstanceData layout mismatch");

    const size_t n = std::min(count, static_cast<size_t>(max_instances_));
    std::memcpy(mapped_instances_, instances, n * sizeof(GpuInstance));
    instance_count_ = n;
    return n;
}

void FrameResources::upload_camera(const CameraUBO& camera, glm::mat4* out_view_proj)
{
    UniformBufferObject ubo{};
    ubo.view = camera.view;
    ubo.proj = camera.proj;
    ubo.lightPos = camera.light_pos;
    ubo.viewPos = camera.view_pos;
    ubo.meters = camera.meters;

    if (out_view_proj)
        *out_view_proj = ubo.proj * ubo.view;

    if (uniform_memory_ == VK_NULL_HANDLE || device_ == VK_NULL_HANDLE)
        return;

    void* data = nullptr;
    vkMapMemory(device_, uniform_memory_, 0, sizeof(ubo), 0, &data);
    if (data)
    {
        std::memcpy(data, &ubo, sizeof(ubo));
        vkUnmapMemory(device_, uniform_memory_);
    }
}
