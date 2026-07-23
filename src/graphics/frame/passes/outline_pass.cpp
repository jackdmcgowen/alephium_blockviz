#include "graphics/pch.h"
#include "graphics/frame/passes/outline_pass.hpp"
#include "graphics/frame/vertex_types.hpp"
#include "graphics/gpu_prv_lib.h"

#include <stdexcept>
#include <vector>

namespace frame_graph
{
namespace
{
static constexpr VkFormat kOutlineColorFormat = VK_FORMAT_R8G8B8A8_UNORM;
} // namespace

void OutlinePass::create(const PassCreateInfo& info)
{
    if (!res_ || !res_->ready())
        throw std::runtime_error("OutlinePass::create: SobelResources not ready");
    if (!info.device || !info.frame_ubo_layout)
        throw std::runtime_error("OutlinePass::create: invalid args");
    create_pipeline_(info);
}

void OutlinePass::destroy(VkDevice device)
{
    destroy_pipeline_(device);
}

void OutlinePass::recreate(const PassCreateInfo& info)
{
    destroy_pipeline_(info.device);
    create_pipeline_(info);
}

void OutlinePass::declare_resources(std::vector<ResourceId>& /*reads*/,
                                    std::vector<ResourceId>& writes) const
{
    writes.push_back(ResourceId::SelectionDepth);
}

void OutlinePass::create_pipeline_(const PassCreateInfo& info)
{
    destroy_pipeline_(info.device);

    std::vector<uint8_t> vert_code, frag_code;
    load_shader_source("vert.spv", vert_code);
    load_shader_source("outline_color_frag.spv", frag_code);
    VkShaderModule vert = VK_NULL_HANDLE, frag = VK_NULL_HANDLE;
    create_shader_module(info.device, vert, vert_code);
    create_shader_module(info.device, frag, frag_code);

    outline_layout_ = create_pipeline_layout(info.device, &info.frame_ubo_layout, 1);

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binds[2]{};
    binds[0].binding = 0;
    binds[0].stride = sizeof(VertexNormal);
    binds[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    binds[1].binding = 1;
    binds[1].stride = sizeof(InstanceData);
    binds[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    VkVertexInputAttributeDescription attrs[6]{};
    attrs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VertexNormal, pos) };
    attrs[1] = { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VertexNormal, normal) };
    attrs[2] = { 2, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(InstanceData, pos) };
    attrs[3] = { 3, 1, VK_FORMAT_R32_SFLOAT, offsetof(InstanceData, scale) };
    attrs[4] = { 4, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(InstanceData, color) };
    attrs[5] = { 5, 1, VK_FORMAT_R32_SFLOAT, offsetof(InstanceData, alpha) };

    GraphicsPipelineCreateInfo ginfo{};
    ginfo.layout = outline_layout_;
    ginfo.stages = stages;
    ginfo.stage_count = 2;
    ginfo.bindings = binds;
    ginfo.binding_count = 2;
    ginfo.attributes = attrs;
    ginfo.attribute_count = 6;
    ginfo.depth_test = true;
    ginfo.depth_write = true;
    ginfo.depth_compare = VK_COMPARE_OP_LESS;
    ginfo.blend_mode = PipelineBlendMode::None;
    ginfo.color_format = kOutlineColorFormat;
    ginfo.color_attachment_count = 1;
    ginfo.depth_format = info.depth_format;
    ginfo.dynamic_viewport_scissor = true;
    ginfo.dynamic_primitive_topology = false;

    try
    {
        outline_pipeline_ = create_graphics_pipeline(info.device, ginfo);
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

void OutlinePass::destroy_pipeline_(VkDevice device)
{
    if (!device)
        return;
    if (outline_pipeline_)
    {
        vkDestroyPipeline(device, outline_pipeline_, nullptr);
        outline_pipeline_ = VK_NULL_HANDLE;
    }
    if (outline_layout_)
    {
        vkDestroyPipelineLayout(device, outline_layout_, nullptr);
        outline_layout_ = VK_NULL_HANDLE;
    }
}

void OutlinePass::record(const PassRecordParams& p)
{
    if (!ready() || !p.base.cmd)
        throw std::runtime_error("OutlinePass::record: not ready");

    PassProfileScope profile(*this, p);
    const VkCommandBuffer cmd = p.base.cmd;
    const uint32_t w = p.width ? p.width : res_->width();
    const uint32_t h = p.height ? p.height : res_->height();

    cmd_image_barrier_aspect(cmd, res_->sel_depth_image(),
                  VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                  0, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_NONE,
                  VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                  VK_IMAGE_ASPECT_DEPTH_BIT);

    cmd_image_barrier_aspect(cmd, res_->outline_color_image(),
                  VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  0, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_NONE,
                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_IMAGE_ASPECT_COLOR_BIT);

    VkRenderingAttachmentInfo color_att{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    color_att.imageView = res_->outline_color_view();
    color_att.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color_att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_att.clearValue.color = { { 0.f, 0.f, 0.f, 0.f } };

    VkRenderingAttachmentInfo depth_att{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    depth_att.imageView = res_->sel_depth_view();
    depth_att.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth_att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depth_att.clearValue.depthStencil = { 1.0f, 0 };

    VkRenderingInfo ri{ VK_STRUCTURE_TYPE_RENDERING_INFO };
    ri.renderArea = { { 0, 0 }, { w, h } };
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &color_att;
    ri.pDepthAttachment = &depth_att;
    vkCmdBeginRendering(cmd, &ri);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, outline_pipeline_);
    VkViewport vp{};
    vp.width = static_cast<float>(w);
    vp.height = static_cast<float>(h);
    vp.minDepth = 0.f;
    vp.maxDepth = 1.f;
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D sc{ { 0, 0 }, { w, h } };
    vkCmdSetScissor(cmd, 0, 1, &sc);

    VkBuffer bufs[] = { p.vertex_buffer, p.outline_instance_buffer };
    VkDeviceSize offs[] = { 0, 0 };
    vkCmdBindVertexBuffers(cmd, 0, 2, bufs, offs);
    vkCmdBindIndexBuffer(cmd, p.index_buffer, 0, VK_INDEX_TYPE_UINT16);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, outline_layout_,
                            0, 1, &p.frame_ubo_set, 0, nullptr);

    const uint32_t n = (p.outline_count > 0)
                           ? (p.outline_count < kMaxSobelInstances
                                  ? p.outline_count
                                  : kMaxSobelInstances)
                           : 0u;
    if (n > 0)
        vkCmdDrawIndexed(cmd, p.index_count ? p.index_count : 36u, n, 0, 0, 0);

    vkCmdEndRendering(cmd);
}

void OutlinePass::record_sel_depth_release_for_compute(VkCommandBuffer cmd) const
{
    if (!res_)
        return;
    const bool split = (res_->graphics_family() != res_->compute_family());
    cmd_image_barrier_aspect(cmd, res_->sel_depth_image(),
                  VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                  VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, 0,
                  VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                  VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
                  VK_IMAGE_ASPECT_DEPTH_BIT,
                  split ? res_->graphics_family() : VK_QUEUE_FAMILY_IGNORED,
                  split ? res_->compute_family() : VK_QUEUE_FAMILY_IGNORED);
}

void OutlinePass::record_outline_color_to_shader_read(VkCommandBuffer cmd) const
{
    if (!res_)
        return;
    cmd_image_barrier_aspect(cmd, res_->outline_color_image(),
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                  VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                  VK_IMAGE_ASPECT_COLOR_BIT);
}

} // namespace frame_graph
