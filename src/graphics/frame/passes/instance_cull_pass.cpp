#include "graphics/pch.h"
#include "graphics/frame/passes/instance_cull_pass.hpp"
#include "graphics/gpu_prv_lib.h"

#include <cstring>
#include <stdexcept>

namespace frame_graph
{
namespace
{
struct CullPC
{
    float planes[6][4];
    uint32_t count;
    float half_extent;
    float pad0;
    float pad1;
};
} // namespace

void InstanceCullPass::create(const PassCreateInfo& info)
{
    destroy(info.device);
    if (!info.device)
        throw std::runtime_error("InstanceCullPass::create: null device");
    device_ = info.device;

    const DescriptorBinding binds[] = {
        { 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT },
        { 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT },
        { 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT },
    };
    set_layout_ = create_descriptor_set_layout(device_, binds, 3);

    const VkDescriptorPoolSize sizes[] = {
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 },
    };
    pool_ = create_descriptor_pool(device_, 1, sizes, 1);
    if (!allocate_descriptor_sets(device_, pool_, &set_layout_, 1, &set_))
        throw std::runtime_error("InstanceCullPass: allocate descriptor set failed");

    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcr.offset = 0;
    pcr.size = sizeof(CullPC);

    layout_ = create_pipeline_layout(device_, &set_layout_, 1, &pcr, 1);

    PipelineCreateInfo pci{};
    pci.type = PipelineType::CMP;
    pci.layout = layout_;
    pci.compute_spv_path = "instance_cull.comp.spv";
    pci.compute_entry = "main";
    pipeline_ = create_pipeline(device_, pci);
}

void InstanceCullPass::destroy(VkDevice device)
{
    if (!device && !device_)
        return;
    VkDevice dev = device ? device : device_;
    destroy_pipeline(dev, pipeline_);
    pipeline_ = VK_NULL_HANDLE;
    destroy_pipeline_layout(dev, layout_);
    layout_ = VK_NULL_HANDLE;
    if (pool_)
    {
        destroy_descriptor_pool(dev, pool_);
        pool_ = VK_NULL_HANDLE;
        set_ = VK_NULL_HANDLE;
    }
    if (set_layout_)
    {
        destroy_descriptor_set_layout(dev, set_layout_);
        set_layout_ = VK_NULL_HANDLE;
    }
    bound_in_ = bound_out_ = bound_draw_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE;
}

void InstanceCullPass::write_descriptors_(VkBuffer in_buf, VkBuffer out_buf, VkBuffer draw_buf)
{
    if (in_buf == bound_in_ && out_buf == bound_out_ && draw_buf == bound_draw_)
        return;

    const DescriptorBufferWrite writes[] = {
        { set_, 0, in_buf, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER },
        { set_, 1, out_buf, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER },
        { set_, 2, draw_buf, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER },
    };
    write_descriptor_buffers(device_, writes, 3);
    bound_in_ = in_buf;
    bound_out_ = out_buf;
    bound_draw_ = draw_buf;
}

void InstanceCullPass::record_cull(const InstanceCullRecordParams& p)
{
    if (!ready() || !p.cmd || !p.in_instances || !p.out_instances || !p.draw_args)
        throw std::runtime_error("InstanceCullPass::record_cull: not ready");
    if (p.instance_count == 0)
        return;

    FrameProfiler::CpuScope cpu(p.profiler, name());
    FrameProfiler::GpuScope gpu(
        p.profiler, p.cmd, name(),
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);

    write_descriptors_(p.in_instances, p.out_instances, p.draw_args);

    // Host-visible draw args were reset; ensure visible to compute.
    // Instance buffer host write → compute read.
    {
        VkMemoryBarrier2 mem{};
        mem.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
        mem.srcStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
        mem.srcAccessMask = VK_ACCESS_2_HOST_WRITE_BIT;
        mem.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        mem.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                            VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
        dep.memoryBarrierCount = 1;
        dep.pMemoryBarriers = &mem;
        vkCmdPipelineBarrier2(p.cmd, &dep);
    }

    vkCmdBindPipeline(p.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
    vkCmdBindDescriptorSets(p.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout_, 0, 1, &set_, 0,
                            nullptr);

    CullPC pc{};
    for (int i = 0; i < 6; ++i)
    {
        pc.planes[i][0] = p.planes[i].x;
        pc.planes[i][1] = p.planes[i].y;
        pc.planes[i][2] = p.planes[i].z;
        pc.planes[i][3] = p.planes[i].w;
    }
    pc.count = p.instance_count;
    pc.half_extent = p.half_extent > 0.f ? p.half_extent : 1.05f;
    vkCmdPushConstants(p.cmd, layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

    const uint32_t groups = (p.instance_count + 63u) / 64u;
    vkCmdDispatch(p.cmd, groups, 1, 1);

    // Compute write → classic vertex/indirect and/or mesh shader SSBO read (PR3).
    {
        VkMemoryBarrier2 mem{};
        mem.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
        mem.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        mem.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        mem.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT |
                           VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT |
                           VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
                           VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT |
                           VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT;
        mem.dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT |
                            VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT |
                            VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
        VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
        dep.memoryBarrierCount = 1;
        dep.pMemoryBarriers = &mem;
        vkCmdPipelineBarrier2(p.cmd, &dep);
    }
}

} // namespace frame_graph
