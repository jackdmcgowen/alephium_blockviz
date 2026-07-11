#include "engine/frame_resources.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

void FrameResources::create(const FrameResourcesCreateInfo& info)
{
    if (!info.device || !info.mem_props || !info.buffer_manager || !info.cube_vertices ||
        !info.cube_indices || info.max_instances == 0)
        throw std::runtime_error("FrameResources::create: invalid info");

    destroy(info.device);
    buffers_ = info.buffer_manager;
    max_instances_ = info.max_instances;

    const VkMemoryPropertyFlags host =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    vertex_ = buffers_->create(BufferDesc{
        info.cube_vertex_bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, host, "cube.vb"});
    {
        void* data = vertex_.map(info.device);
        if (data)
            std::memcpy(data, info.cube_vertices, info.cube_vertex_bytes);
        vertex_.unmap(info.device);
    }

    index_ = buffers_->create(BufferDesc{
        info.cube_index_bytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, host, "cube.ib"});
    {
        void* data = index_.map(info.device);
        if (data)
            std::memcpy(data, info.cube_indices, info.cube_index_bytes);
        index_.unmap(info.device);
    }

    const VkDeviceSize inst_size =
        static_cast<VkDeviceSize>(sizeof(InstanceData)) * max_instances_;
    instance_ = buffers_->create(BufferDesc{
        inst_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, host, "cube.instances"});
    mapped_instances_ = instance_.map(info.device);
    instance_count_ = 0;

    uniform_ = buffers_->create(BufferDesc{
        sizeof(UniformBufferObject), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, host, "frame.ubo"});
}

void FrameResources::destroy(VkDevice device)
{
    if (!buffers_)
        return;

    if (mapped_instances_ && instance_.valid())
    {
        instance_.unmap(device);
        mapped_instances_ = nullptr;
    }
    buffers_->destroy(vertex_);
    buffers_->destroy(index_);
    buffers_->destroy(instance_);
    buffers_->destroy(uniform_);
    instance_count_ = 0;
    max_instances_ = 0;
    buffers_ = nullptr;
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

    if (out_view_proj)
        *out_view_proj = ubo.proj * ubo.view;

    if (!uniform_.valid() || !buffers_ || !buffers_->device())
        return;

    void* data = uniform_.map(buffers_->device());
    if (data)
    {
        std::memcpy(data, &ubo, sizeof(ubo));
        uniform_.unmap(buffers_->device());
    }
}
