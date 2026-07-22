#include "graphics/pch.h"
#include "graphics/frame/passes/sobel_compute_pass.hpp"
#include "graphics/gpu_prv_lib.h"

#include <stdexcept>

namespace frame_graph
{
namespace
{
struct SobelPC
{
    float strength;
    float threshold;
    float inv_width;
    float inv_height;
};
} // namespace

void SobelComputePass::create(const PassCreateInfo& info)
{
    if (!res_ || !res_->ready())
        throw std::runtime_error("SobelComputePass::create: SobelResources not ready");
    if (!info.device)
        throw std::runtime_error("SobelComputePass::create: null device");
    create_pipeline_(info.device);
}

void SobelComputePass::destroy(VkDevice device)
{
    destroy_pipeline_(device);
}

void SobelComputePass::recreate(const PassCreateInfo& info)
{
    destroy_pipeline_(info.device);
    create_pipeline_(info.device);
}

void SobelComputePass::declare_resources(std::vector<ResourceId>& reads,
                                         std::vector<ResourceId>& writes) const
{
    reads.push_back(ResourceId::SelectionDepth);
    writes.push_back(ResourceId::SobelEdges);
}

void SobelComputePass::create_pipeline_(VkDevice device)
{
    destroy_pipeline_(device);

    VkDescriptorSetLayout set_layout = res_->compute_set_layout();
    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcr.size = sizeof(SobelPC);

    compute_layout_ = create_pipeline_layout(device, &set_layout, 1, &pcr, 1);

    PipelineCreateInfo pci{};
    pci.type = PipelineType::CMP;
    pci.layout = compute_layout_;
    pci.compute_spv_path = "sobel.comp.spv";
    pci.compute_entry = "main";
    pipeline_ = create_pipeline(device, pci);
}

void SobelComputePass::destroy_pipeline_(VkDevice device)
{
    if (!device)
        return;
    if (pipeline_)
    {
        vkDestroyPipeline(device, pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }
    if (compute_layout_)
    {
        vkDestroyPipelineLayout(device, compute_layout_, nullptr);
        compute_layout_ = VK_NULL_HANDLE;
    }
}

void SobelComputePass::record(const PassRecordParams& p)
{
    if (!ready() || !p.base.cmd)
        throw std::runtime_error("SobelComputePass::record: not ready");

    const VkCommandBuffer cmd = p.base.cmd;
    const uint32_t w = res_->width();
    const uint32_t h = res_->height();
    const bool split = (res_->graphics_family() != res_->compute_family());

    // Acquire (if split) + layout ATTACHMENT → SHADER_READ for sampling (CMP-safe stages).
    cmd_image_barrier_aspect(cmd, res_->sel_depth_image(),
                  VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  0, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                  VK_PIPELINE_STAGE_2_NONE, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                  VK_IMAGE_ASPECT_DEPTH_BIT,
                  split ? res_->graphics_family() : VK_QUEUE_FAMILY_IGNORED,
                  split ? res_->compute_family() : VK_QUEUE_FAMILY_IGNORED);

    cmd_image_barrier_aspect(cmd, res_->edge_image(),
                  VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                  0, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_NONE, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                  VK_IMAGE_ASPECT_COLOR_BIT);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
    VkDescriptorSet set = res_->compute_set();
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, compute_layout_, 0, 1,
                            &set, 0, nullptr);

    SobelPC pc{};
    pc.strength = 14.0f;
    pc.threshold = 0.001f;
    pc.inv_width = 1.0f / static_cast<float>(w);
    pc.inv_height = 1.0f / static_cast<float>(h);
    vkCmdPushConstants(cmd, compute_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

    vkCmdDispatch(cmd, (w + 7) / 8, (h + 7) / 8, 1);

    cmd_image_barrier_aspect(cmd, res_->edge_image(),
                  VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                  VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, 0,
                  VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_2_NONE,
                  VK_IMAGE_ASPECT_COLOR_BIT,
                  split ? res_->compute_family() : VK_QUEUE_FAMILY_IGNORED,
                  split ? res_->graphics_family() : VK_QUEUE_FAMILY_IGNORED);

    if (split)
    {
        cmd_image_barrier_aspect(cmd, res_->sel_depth_image(),
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                      VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, 0,
                      VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_2_NONE,
                      VK_IMAGE_ASPECT_DEPTH_BIT,
                      res_->compute_family(), res_->graphics_family());
    }
}

} // namespace frame_graph
