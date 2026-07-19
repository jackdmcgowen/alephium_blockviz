#include "graphics/pch.h"
#include "graphics/pipelines/sobel_pipeline.hpp"
#include "graphics/frame/vertex_types.hpp"

#include "graphics/gpu_prv_lib.h"

#include <cstring>
#include <stdexcept>
#include <vector>

namespace
{
struct SobelPC
{
    float strength;
    float threshold;
    float inv_width;
    float inv_height;
};

struct OverlayPC
{
    float intensity;
    float pad0;
    float pad1;
    float pad2;
};

static constexpr VkFormat kOutlineColorFormat = VK_FORMAT_R8G8B8A8_UNORM;

} // namespace

void SobelPipeline::create_images(const SobelPipelineCreateInfo& info)
{
    create_image(
        info.device, info.width, info.height, info.depth_format,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        sel_depth_image_, sel_depth_memory_, info.mem_props);
    sel_depth_view_ = create_image_view(info.device, sel_depth_image_, info.depth_format,
                                        VK_IMAGE_ASPECT_DEPTH_BIT);

    create_image(
        info.device, info.width, info.height, kOutlineColorFormat,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        outline_color_image_, outline_color_memory_, info.mem_props);
    outline_color_view_ = create_image_view(info.device, outline_color_image_,
                                            kOutlineColorFormat, VK_IMAGE_ASPECT_COLOR_BIT);

    create_image(
        info.device, info.width, info.height, VK_FORMAT_R8_UNORM,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        edge_image_, edge_memory_, info.mem_props);
    edge_view_ = create_image_view(info.device, edge_image_, VK_FORMAT_R8_UNORM,
                                   VK_IMAGE_ASPECT_COLOR_BIT);
}

void SobelPipeline::create_descriptors(VkDevice device)
{
    VkSamplerCreateInfo samp{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    samp.magFilter = VK_FILTER_NEAREST;
    samp.minFilter = VK_FILTER_NEAREST;
    samp.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samp.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samp.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(device, &samp, nullptr, &depth_sampler_) != VK_SUCCESS ||
        vkCreateSampler(device, &samp, nullptr, &edge_sampler_) != VK_SUCCESS ||
        vkCreateSampler(device, &samp, nullptr, &outline_color_sampler_) != VK_SUCCESS)
        throw std::runtime_error("SobelPipeline: samplers");

    const DescriptorBinding compute_binds[] = {
        { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT },
        { 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT },
    };
    compute_set_layout_ = create_descriptor_set_layout(device, compute_binds, 2);

    const DescriptorBinding overlay_binds[] = {
        { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT },
        { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT },
    };
    overlay_set_layout_ = create_descriptor_set_layout(device, overlay_binds, 2);

    const VkDescriptorPoolSize sizes[] = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 6 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2 },
    };
    descriptor_pool_ = create_descriptor_pool(device, 4, sizes, 2);

    if (!allocate_descriptor_sets(device, descriptor_pool_, &compute_set_layout_, 1,
                                  &compute_set_) ||
        !allocate_descriptor_sets(device, descriptor_pool_, &overlay_set_layout_, 1,
                                  &overlay_set_))
        throw std::runtime_error("SobelPipeline: allocate descriptor sets");

    // Write once at create — never update while CBs are pending (VUID-03047).
    write_static_descriptors_();
}

void SobelPipeline::write_static_descriptors_()
{
    const DescriptorImageWrite compute_imgs[] = {
        { compute_set_, 0, sel_depth_view_, depth_sampler_,
          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
          VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER },
        { compute_set_, 1, edge_view_, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL,
          VK_DESCRIPTOR_TYPE_STORAGE_IMAGE },
    };
    write_descriptor_images(device_, compute_imgs, 2);

    const DescriptorImageWrite overlay_imgs[] = {
        { overlay_set_, 0, edge_view_, edge_sampler_,
          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
          VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER },
        { overlay_set_, 1, outline_color_view_, outline_color_sampler_,
          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
          VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER },
    };
    write_descriptor_images(device_, overlay_imgs, 2);
}

void SobelPipeline::create_compute_pipeline(VkDevice device)
{
    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcr.size = sizeof(SobelPC);

    compute_layout_ = create_pipeline_layout(device, &compute_set_layout_, 1, &pcr, 1);

    PipelineCreateInfo pci{};
    pci.type = PipelineType::CMP;
    pci.layout = compute_layout_;
    pci.compute_spv_path = "sobel.comp.spv";
    pci.compute_entry = "main";
    pipeline_ = create_pipeline(device, pci);
}

void SobelPipeline::create_outline_pipeline(VkDevice device, VkFormat depth_format,
                                            VkDescriptorSetLayout ubo_layout)
{
    std::vector<uint8_t> vert_code, frag_code;
    load_shader_source("vert.spv", vert_code);
    load_shader_source("outline_color_frag.spv", frag_code);
    VkShaderModule vert = VK_NULL_HANDLE, frag = VK_NULL_HANDLE;
    create_shader_module(device, vert, vert_code);
    create_shader_module(device, frag, frag_code);

    outline_layout_ = create_pipeline_layout(device, &ubo_layout, 1);

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
    ginfo.depth_format = depth_format;
    ginfo.dynamic_viewport_scissor = true;
    ginfo.dynamic_primitive_topology = false;

    try
    {
        outline_pipeline_ = create_graphics_pipeline(device, ginfo);
    }
    catch (...)
    {
        destroy_shader_module(device, vert);
        destroy_shader_module(device, frag);
        throw;
    }

    destroy_shader_module(device, vert);
    destroy_shader_module(device, frag);
}

void SobelPipeline::create_overlay_pipeline(VkDevice device, VkFormat color_format)
{
    std::vector<uint8_t> vert_code, frag_code;
    load_shader_source("edge_overlay_vert.spv", vert_code);
    load_shader_source("edge_overlay_frag.spv", frag_code);
    VkShaderModule vert = VK_NULL_HANDLE, frag = VK_NULL_HANDLE;
    create_shader_module(device, vert, vert_code);
    create_shader_module(device, frag, frag_code);

    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pcr.size = sizeof(OverlayPC);

    overlay_layout_ =
        create_pipeline_layout(device, &overlay_set_layout_, 1, &pcr, 1);

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
    ginfo.color_format = color_format;
    ginfo.color_attachment_count = 1;
    ginfo.dynamic_viewport_scissor = true;

    try
    {
        overlay_pipeline_ = create_graphics_pipeline(device, ginfo);
    }
    catch (...)
    {
        destroy_shader_module(device, vert);
        destroy_shader_module(device, frag);
        throw;
    }

    destroy_shader_module(device, vert);
    destroy_shader_module(device, frag);
}

void SobelPipeline::create_sync(VkDevice device)
{
    VkSemaphoreCreateInfo sci{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    for (uint32_t i = 0; i < kMaxFrames; ++i)
    {
        if (vkCreateSemaphore(device, &sci, nullptr, &compute_finished_[i]) != VK_SUCCESS ||
            vkCreateSemaphore(device, &sci, nullptr, &graphics_to_compute_[i]) != VK_SUCCESS)
            throw std::runtime_error("SobelPipeline: per-frame semaphores");
    }
}

VkSemaphore SobelPipeline::graphics_to_compute(uint32_t frame_index) const
{
    return graphics_to_compute_[frame_index % kMaxFrames];
}

VkSemaphore SobelPipeline::compute_finished(uint32_t frame_index) const
{
    return compute_finished_[frame_index % kMaxFrames];
}

void SobelPipeline::create(const SobelPipelineCreateInfo& info)
{
    if (!info.device || !info.mem_props || !info.frame_ubo_layout ||
        info.width == 0 || info.height == 0)
        throw std::runtime_error("SobelPipeline::create: invalid args");

    device_ = info.device;
    width_ = info.width;
    height_ = info.height;
    graphics_family_ = info.graphics_family;
    compute_family_ = info.compute_family;
    depth_format_ = info.depth_format;

    create_images(info);
    create_descriptors(info.device);
    create_compute_pipeline(info.device);
    create_outline_pipeline(info.device, info.depth_format, info.frame_ubo_layout);
    create_overlay_pipeline(info.device, VK_FORMAT_R8G8B8A8_SRGB);
    create_sync(info.device);
}

void SobelPipeline::destroy(VkDevice device)
{
    if (!device)
        return;

    for (uint32_t i = 0; i < kMaxFrames; ++i)
    {
        if (graphics_to_compute_[i] != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(device, graphics_to_compute_[i], nullptr);
            graphics_to_compute_[i] = VK_NULL_HANDLE;
        }
        if (compute_finished_[i] != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(device, compute_finished_[i], nullptr);
            compute_finished_[i] = VK_NULL_HANDLE;
        }
    }

    if (overlay_pipeline_) { vkDestroyPipeline(device, overlay_pipeline_, nullptr); overlay_pipeline_ = VK_NULL_HANDLE; }
    if (overlay_layout_) { vkDestroyPipelineLayout(device, overlay_layout_, nullptr); overlay_layout_ = VK_NULL_HANDLE; }
    if (outline_pipeline_) { vkDestroyPipeline(device, outline_pipeline_, nullptr); outline_pipeline_ = VK_NULL_HANDLE; }
    if (outline_layout_) { vkDestroyPipelineLayout(device, outline_layout_, nullptr); outline_layout_ = VK_NULL_HANDLE; }
    if (pipeline_) { vkDestroyPipeline(device, pipeline_, nullptr); pipeline_ = VK_NULL_HANDLE; }
    if (compute_layout_) { vkDestroyPipelineLayout(device, compute_layout_, nullptr); compute_layout_ = VK_NULL_HANDLE; }
    if (descriptor_pool_) {
        destroy_descriptor_pool(device, descriptor_pool_);
        descriptor_pool_ = VK_NULL_HANDLE;
        compute_set_ = overlay_set_ = VK_NULL_HANDLE;
    }
    if (compute_set_layout_)
    {
        destroy_descriptor_set_layout(device, compute_set_layout_);
        compute_set_layout_ = VK_NULL_HANDLE;
    }
    if (overlay_set_layout_)
    {
        destroy_descriptor_set_layout(device, overlay_set_layout_);
        overlay_set_layout_ = VK_NULL_HANDLE;
    }
    if (depth_sampler_) { vkDestroySampler(device, depth_sampler_, nullptr); depth_sampler_ = VK_NULL_HANDLE; }
    if (edge_sampler_) { vkDestroySampler(device, edge_sampler_, nullptr); edge_sampler_ = VK_NULL_HANDLE; }
    if (outline_color_sampler_) { vkDestroySampler(device, outline_color_sampler_, nullptr); outline_color_sampler_ = VK_NULL_HANDLE; }
    if (edge_view_) { destroy_image_view(device, edge_view_); edge_view_ = VK_NULL_HANDLE; }
    if (edge_image_) { destroy_image(device, edge_image_, edge_memory_); edge_image_ = VK_NULL_HANDLE; edge_memory_ = VK_NULL_HANDLE; }
    if (outline_color_view_) { destroy_image_view(device, outline_color_view_); outline_color_view_ = VK_NULL_HANDLE; }
    if (outline_color_image_) {
        destroy_image(device, outline_color_image_, outline_color_memory_);
        outline_color_image_ = VK_NULL_HANDLE;
        outline_color_memory_ = VK_NULL_HANDLE;
    }
    if (sel_depth_view_) { destroy_image_view(device, sel_depth_view_); sel_depth_view_ = VK_NULL_HANDLE; }
    if (sel_depth_image_) { destroy_image(device, sel_depth_image_, sel_depth_memory_); sel_depth_image_ = VK_NULL_HANDLE; sel_depth_memory_ = VK_NULL_HANDLE; }
}

void SobelPipeline::recreate(const SobelPipelineCreateInfo& info)
{
    destroy(info.device);
    create(info);
}

void SobelPipeline::record_outline_pass(const OutlinePassDrawParams& p)
{
    // sel_depth → DEPTH_ATTACHMENT
    cmd_image_barrier_aspect(p.cmd, sel_depth_image_,
                  VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                  0, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_NONE,
                  VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                  VK_IMAGE_ASPECT_DEPTH_BIT);

    // outline_color → COLOR_ATTACHMENT
    cmd_image_barrier_aspect(p.cmd, outline_color_image_,
                  VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  0, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_NONE,
                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_IMAGE_ASPECT_COLOR_BIT);

    VkRenderingAttachmentInfo color_att{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    color_att.imageView = outline_color_view_;
    color_att.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color_att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_att.clearValue.color = { { 0.f, 0.f, 0.f, 0.f } };

    VkRenderingAttachmentInfo depth_att{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    depth_att.imageView = sel_depth_view_;
    depth_att.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth_att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depth_att.clearValue.depthStencil = { 1.0f, 0 };

    VkRenderingInfo ri{ VK_STRUCTURE_TYPE_RENDERING_INFO };
    ri.renderArea = { { 0, 0 }, { p.width, p.height } };
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &color_att;
    ri.pDepthAttachment = &depth_att;
    vkCmdBeginRendering(p.cmd, &ri);

    vkCmdBindPipeline(p.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, outline_pipeline_);
    VkViewport vp{};
    vp.width = static_cast<float>(p.width);
    vp.height = static_cast<float>(p.height);
    vp.minDepth = 0.f;
    vp.maxDepth = 1.f;
    vkCmdSetViewport(p.cmd, 0, 1, &vp);
    VkRect2D sc{ { 0, 0 }, { p.width, p.height } };
    vkCmdSetScissor(p.cmd, 0, 1, &sc);

    VkBuffer bufs[] = { p.vertex_buffer, p.outline_instance_buffer };
    VkDeviceSize offs[] = { 0, 0 };
    vkCmdBindVertexBuffers(p.cmd, 0, 2, bufs, offs);
    vkCmdBindIndexBuffer(p.cmd, p.index_buffer, 0, VK_INDEX_TYPE_UINT16);
    vkCmdBindDescriptorSets(p.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, outline_layout_,
                            0, 1, &p.ubo_set, 0, nullptr);

    // One clear; one multi-instance draw of all outline cubes (union silhouette + colors).
    const uint32_t n = (p.outline_count > 0)
                           ? (p.outline_count < kMaxSobelInstances
                                  ? p.outline_count
                                  : kMaxSobelInstances)
                           : 0u;
    if (n > 0)
        vkCmdDrawIndexed(p.cmd, p.index_count, n, 0, 0, 0);

    vkCmdEndRendering(p.cmd);
}

void SobelPipeline::record_sel_depth_release_for_compute(VkCommandBuffer cmd)
{
    const bool split = (graphics_family_ != compute_family_);
    // Execution dependency only for depth write → external/compute. Keep DEPTH_ATTACHMENT
    // layout; compute CB performs ATTACHMENT → SHADER_READ (valid stages on CMP queue).
    cmd_image_barrier_aspect(cmd, sel_depth_image_,
                  VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                  VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, 0,
                  VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                  VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
                  VK_IMAGE_ASPECT_DEPTH_BIT,
                  split ? graphics_family_ : VK_QUEUE_FAMILY_IGNORED,
                  split ? compute_family_ : VK_QUEUE_FAMILY_IGNORED);
}

void SobelPipeline::record_outline_color_to_shader_read(VkCommandBuffer cmd)
{
    cmd_image_barrier_aspect(cmd, outline_color_image_,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                  VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                  VK_IMAGE_ASPECT_COLOR_BIT);
}

void SobelPipeline::record_dispatch(VkCommandBuffer cmd, float strength, float threshold)
{
    const bool split = (graphics_family_ != compute_family_);

    // Acquire (if split) + layout ATTACHMENT → SHADER_READ for sampling (CMP-safe stages).
    cmd_image_barrier_aspect(cmd, sel_depth_image_,
                  VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  0, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                  VK_PIPELINE_STAGE_2_NONE, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                  VK_IMAGE_ASPECT_DEPTH_BIT,
                  split ? graphics_family_ : VK_QUEUE_FAMILY_IGNORED,
                  split ? compute_family_ : VK_QUEUE_FAMILY_IGNORED);

    // Discard prior contents → GENERAL for storage write (no FRAGMENT stages on CMP).
    cmd_image_barrier_aspect(cmd, edge_image_,
                  VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                  0, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_NONE, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                  VK_IMAGE_ASPECT_COLOR_BIT);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, compute_layout_, 0, 1,
                            &compute_set_, 0, nullptr);

    SobelPC pc{};
    pc.strength = strength;
    pc.threshold = threshold;
    pc.inv_width = 1.0f / static_cast<float>(width_);
    pc.inv_height = 1.0f / static_cast<float>(height_);
    vkCmdPushConstants(cmd, compute_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

    vkCmdDispatch(cmd, (width_ + 7) / 8, (height_ + 7) / 8, 1);

    // Make edge write available; keep GENERAL — graphics does GENERAL→SHADER_READ after wait.
    cmd_image_barrier_aspect(cmd, edge_image_,
                  VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                  VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, 0,
                  VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_2_NONE,
                  VK_IMAGE_ASPECT_COLOR_BIT,
                  split ? compute_family_ : VK_QUEUE_FAMILY_IGNORED,
                  split ? graphics_family_ : VK_QUEUE_FAMILY_IGNORED);

    // Return sel_depth ownership to graphics. Layout can stay SHADER_READ; next graphics
    // pass uses UNDEFINED → DEPTH_ATTACHMENT (discard).
    if (split)
    {
        cmd_image_barrier_aspect(cmd, sel_depth_image_,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                      VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, 0,
                      VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_2_NONE,
                      VK_IMAGE_ASPECT_DEPTH_BIT,
                      compute_family_, graphics_family_);
    }
}

void SobelPipeline::record_edge_acquire_for_graphics(VkCommandBuffer cmd)
{
    const bool split = (graphics_family_ != compute_family_);
    // After compute_finished wait: own edge on graphics, GENERAL → SHADER_READ for sampling.
    cmd_image_barrier_aspect(cmd, edge_image_,
                  VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  0, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                  VK_PIPELINE_STAGE_2_NONE, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                  VK_IMAGE_ASPECT_COLOR_BIT,
                  split ? compute_family_ : VK_QUEUE_FAMILY_IGNORED,
                  split ? graphics_family_ : VK_QUEUE_FAMILY_IGNORED);
}

void SobelPipeline::record_overlay(VkCommandBuffer cmd, uint32_t width, uint32_t height,
                                  float intensity)
{
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, overlay_pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, overlay_layout_, 0, 1,
                            &overlay_set_, 0, nullptr);

    VkViewport vp{};
    vp.width = static_cast<float>(width);
    vp.height = static_cast<float>(height);
    vp.maxDepth = 1.f;
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D sc{ { 0, 0 }, { width, height } };
    vkCmdSetScissor(cmd, 0, 1, &sc);

    OverlayPC pc{};
    pc.intensity = intensity;
    vkCmdPushConstants(cmd, overlay_layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}
