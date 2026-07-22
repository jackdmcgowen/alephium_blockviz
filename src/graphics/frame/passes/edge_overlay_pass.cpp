#include "graphics/pch.h"
#include "graphics/frame/passes/edge_overlay_pass.hpp"
#include "graphics/gpu_prv_lib.h"

#include <stdexcept>
#include <vector>

namespace frame_graph
{
namespace
{
struct OverlayPC
{
    float intensity;
    float pad0;
    float pad1;
    float pad2;
};
} // namespace

void EdgeOverlayPass::create(const PassCreateInfo& info)
{
    if (!res_ || !res_->ready())
        throw std::runtime_error("EdgeOverlayPass::create: SobelResources not ready");
    if (!info.device || info.color_format == VK_FORMAT_UNDEFINED)
        throw std::runtime_error("EdgeOverlayPass::create: invalid args");
    create_pipeline_(info);
}

void EdgeOverlayPass::destroy(VkDevice device)
{
    destroy_pipeline_(device);
}

void EdgeOverlayPass::recreate(const PassCreateInfo& info)
{
    destroy_pipeline_(info.device);
    create_pipeline_(info);
}

void EdgeOverlayPass::declare_resources(std::vector<ResourceId>& reads,
                                        std::vector<ResourceId>& writes) const
{
    reads.push_back(ResourceId::SobelEdges);
    writes.push_back(ResourceId::SwapchainColor);
}

void EdgeOverlayPass::create_pipeline_(const PassCreateInfo& info)
{
    destroy_pipeline_(info.device);

    std::vector<uint8_t> vert_code, frag_code;
    load_shader_source("edge_overlay_vert.spv", vert_code);
    load_shader_source("edge_overlay_frag.spv", frag_code);
    VkShaderModule vert = VK_NULL_HANDLE, frag = VK_NULL_HANDLE;
    create_shader_module(info.device, vert, vert_code);
    create_shader_module(info.device, frag, frag_code);

    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pcr.size = sizeof(OverlayPC);

    VkDescriptorSetLayout set_layout = res_->overlay_set_layout();
    overlay_layout_ = create_pipeline_layout(info.device, &set_layout, 1, &pcr, 1);

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                  VK_SHADER_STAGE_VERTEX_BIT, vert, "main", nullptr };
    stages[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                  VK_SHADER_STAGE_FRAGMENT_BIT, frag, "main", nullptr };

    GraphicsPipelineCreateInfo ginfo{};
    ginfo.layout = overlay_layout_;
    ginfo.stages = stages;
    ginfo.stage_count = 2;
    ginfo.binding_count = 0;
    ginfo.attribute_count = 0;
    ginfo.cull_mode = VK_CULL_MODE_NONE;
    ginfo.depth_test = false;
    ginfo.depth_write = false;
    ginfo.blend_mode = PipelineBlendMode::Premultiplied;
    ginfo.color_format = info.color_format;
    ginfo.color_attachment_count = 1;
    ginfo.dynamic_viewport_scissor = true;

    try
    {
        overlay_pipeline_ = create_graphics_pipeline(info.device, ginfo);
    }
    catch (...)
    {
        destroy_shader_module(info.device, vert);
        destroy_shader_module(info.device, frag);
        destroy_pipeline_(info.device);
        throw;
    }

    destroy_shader_module(info.device, vert);
    destroy_shader_module(info.device, frag);
}

void EdgeOverlayPass::destroy_pipeline_(VkDevice device)
{
    if (!device)
        return;
    if (overlay_pipeline_)
    {
        vkDestroyPipeline(device, overlay_pipeline_, nullptr);
        overlay_pipeline_ = VK_NULL_HANDLE;
    }
    if (overlay_layout_)
    {
        vkDestroyPipelineLayout(device, overlay_layout_, nullptr);
        overlay_layout_ = VK_NULL_HANDLE;
    }
}

void EdgeOverlayPass::record_edge_acquire_for_graphics(VkCommandBuffer cmd) const
{
    if (!res_)
        return;
    const bool split = (res_->graphics_family() != res_->compute_family());
    cmd_image_barrier_aspect(cmd, res_->edge_image(),
                  VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  0, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                  VK_PIPELINE_STAGE_2_NONE, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                  VK_IMAGE_ASPECT_COLOR_BIT,
                  split ? res_->compute_family() : VK_QUEUE_FAMILY_IGNORED,
                  split ? res_->graphics_family() : VK_QUEUE_FAMILY_IGNORED);
}

void EdgeOverlayPass::record(const PassRecordParams& p)
{
    if (!ready() || !p.base.cmd)
        throw std::runtime_error("EdgeOverlayPass::record: not ready");

    const VkCommandBuffer cmd = p.base.cmd;
    const uint32_t w = p.width ? p.width : res_->width();
    const uint32_t h = p.height ? p.height : res_->height();

    record_edge_acquire_for_graphics(cmd);

    // Caller may already be inside a rendering pass (async path begins rendering around us).
    // When color_view is set, begin/end rendering here for a self-contained graph node.
    const bool own_rendering = (p.color_view != VK_NULL_HANDLE);
    if (own_rendering)
    {
        VkRenderingAttachmentInfo color{};
        color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        color.imageView = p.color_view;
        color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

        VkRenderingInfo ri{ VK_STRUCTURE_TYPE_RENDERING_INFO };
        ri.renderArea = { { 0, 0 }, { w, h } };
        ri.layerCount = 1;
        ri.colorAttachmentCount = 1;
        ri.pColorAttachments = &color;
        vkCmdBeginRendering(cmd, &ri);
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, overlay_pipeline_);
    VkDescriptorSet set = res_->overlay_set();
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, overlay_layout_, 0, 1,
                            &set, 0, nullptr);

    VkViewport vp{};
    vp.width = static_cast<float>(w);
    vp.height = static_cast<float>(h);
    vp.maxDepth = 1.f;
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D sc{ { 0, 0 }, { w, h } };
    vkCmdSetScissor(cmd, 0, 1, &sc);

    OverlayPC pc{};
    pc.intensity = 1.0f;
    vkCmdPushConstants(cmd, overlay_layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdDraw(cmd, 3, 1, 0, 0);

    if (own_rendering)
        vkCmdEndRendering(cmd);
}

} // namespace frame_graph
