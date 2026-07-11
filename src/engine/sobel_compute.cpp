#include "engine/sobel_compute.hpp"

#include "graphics/gpu_prv_lib.h"

#include <array>
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
    float highlight[4];
};
} // namespace

void SobelCompute::create_edge_image(const SobelComputeCreateInfo& info)
{
    create_image(
        info.device,
        info.width, info.height,
        VK_FORMAT_R8_UNORM,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        edge_image_,
        edge_memory_,
        info.mem_props);
    edge_view_ = create_image_view(info.device, edge_image_, VK_FORMAT_R8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);
}

void SobelCompute::create_descriptors(VkDevice device)
{
    // Depth sampler (compute)
    VkSamplerCreateInfo samp{};
    samp.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samp.magFilter = VK_FILTER_NEAREST;
    samp.minFilter = VK_FILTER_NEAREST;
    samp.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samp.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samp.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(device, &samp, nullptr, &depth_sampler_) != VK_SUCCESS)
        throw std::runtime_error("SobelCompute: depth sampler");

    if (vkCreateSampler(device, &samp, nullptr, &edge_sampler_) != VK_SUCCESS)
        throw std::runtime_error("SobelCompute: edge sampler");

    // Compute set: 0 = depth combined, 1 = edge storage
    VkDescriptorSetLayoutBinding binds[2]{};
    binds[0].binding = 0;
    binds[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binds[0].descriptorCount = 1;
    binds[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    binds[1].binding = 1;
    binds[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    binds[1].descriptorCount = 1;
    binds[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layout_ci{};
    layout_ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_ci.bindingCount = 2;
    layout_ci.pBindings = binds;
    if (vkCreateDescriptorSetLayout(device, &layout_ci, nullptr, &compute_set_layout_) != VK_SUCCESS)
        throw std::runtime_error("SobelCompute: compute set layout");

    // Overlay set: 0 = edge sampled
    VkDescriptorSetLayoutBinding ob{};
    ob.binding = 0;
    ob.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ob.descriptorCount = 1;
    ob.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo olayout{};
    olayout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    olayout.bindingCount = 1;
    olayout.pBindings = &ob;
    if (vkCreateDescriptorSetLayout(device, &olayout, nullptr, &overlay_set_layout_) != VK_SUCCESS)
        throw std::runtime_error("SobelCompute: overlay set layout");

    VkDescriptorPoolSize sizes[] = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2 },
    };
    VkDescriptorPoolCreateInfo pool_ci{};
    pool_ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_ci.maxSets = 4;
    pool_ci.poolSizeCount = 2;
    pool_ci.pPoolSizes = sizes;
    pool_ci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    if (vkCreateDescriptorPool(device, &pool_ci, nullptr, &descriptor_pool_) != VK_SUCCESS)
        throw std::runtime_error("SobelCompute: descriptor pool");

    VkDescriptorSetAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc.descriptorPool = descriptor_pool_;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &compute_set_layout_;
    if (vkAllocateDescriptorSets(device, &alloc, &compute_set_) != VK_SUCCESS)
        throw std::runtime_error("SobelCompute: compute set");

    alloc.pSetLayouts = &overlay_set_layout_;
    if (vkAllocateDescriptorSets(device, &alloc, &overlay_set_) != VK_SUCCESS)
        throw std::runtime_error("SobelCompute: overlay set");
}

void SobelCompute::create_compute_pipeline(VkDevice device)
{
    std::vector<uint8_t> code;
    load_shader_source("sobel.comp.spv", code);
    VkShaderModule mod = VK_NULL_HANDLE;
    create_shader_module(device, mod, code);

    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcr.offset = 0;
    pcr.size = sizeof(SobelPC);

    VkPipelineLayoutCreateInfo plci{};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &compute_set_layout_;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcr;
    if (vkCreatePipelineLayout(device, &plci, nullptr, &compute_layout_) != VK_SUCCESS)
        throw std::runtime_error("SobelCompute: compute layout");

    VkPipelineShaderStageCreateInfo stage{};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = mod;
    stage.pName = "main";

    VkComputePipelineCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    ci.stage = stage;
    ci.layout = compute_layout_;
    if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &ci, nullptr, &pipeline_) != VK_SUCCESS)
        throw std::runtime_error("SobelCompute: compute pipeline");

    destroy_shader_module(device, mod);
}

void SobelCompute::create_overlay_pipeline(VkDevice device, VkFormat color_format, VkFormat /*depth_format*/)
{
    std::vector<uint8_t> vert_code, frag_code;
    load_shader_source("edge_overlay_vert.spv", vert_code);
    load_shader_source("edge_overlay_frag.spv", frag_code);
    VkShaderModule vert = VK_NULL_HANDLE, frag = VK_NULL_HANDLE;
    create_shader_module(device, vert, vert_code);
    create_shader_module(device, frag, frag_code);

    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pcr.offset = 0;
    pcr.size = sizeof(OverlayPC);

    VkPipelineLayoutCreateInfo plci{};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &overlay_set_layout_;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcr;
    if (vkCreatePipelineLayout(device, &plci, nullptr, &overlay_layout_) != VK_SUCCESS)
        throw std::runtime_error("SobelCompute: overlay layout");

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_FALSE;
    ds.depthWriteEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState blend{};
    blend.blendEnable = VK_TRUE;
    blend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.colorBlendOp = VK_BLEND_OP_ADD;
    blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.alphaBlendOp = VK_BLEND_OP_ADD;
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &blend;

    VkDynamicState dyn_states[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };
    VkPipelineDynamicStateCreateInfo dyn{};
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dyn_states;

    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &color_format;

    VkGraphicsPipelineCreateInfo gci{};
    gci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gci.pNext = &rendering;
    gci.stageCount = 2;
    gci.pStages = stages;
    gci.pVertexInputState = &vi;
    gci.pInputAssemblyState = &ia;
    gci.pViewportState = &vp;
    gci.pRasterizationState = &rs;
    gci.pMultisampleState = &ms;
    gci.pDepthStencilState = &ds;
    gci.pColorBlendState = &cb;
    gci.pDynamicState = &dyn;
    gci.layout = overlay_layout_;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gci, nullptr, &overlay_pipeline_) != VK_SUCCESS)
        throw std::runtime_error("SobelCompute: overlay pipeline");

    destroy_shader_module(device, vert);
    destroy_shader_module(device, frag);

    // Bind edge image to overlay set (static write; view recreated on resize → rewrite)
    VkDescriptorImageInfo img{};
    img.sampler = edge_sampler_;
    img.imageView = edge_view_;
    img.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet w{};
    w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet = overlay_set_;
    w.dstBinding = 0;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.descriptorCount = 1;
    w.pImageInfo = &img;
    vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);
}

void SobelCompute::create_sync(VkDevice device)
{
    VkSemaphoreCreateInfo sci{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    if (vkCreateSemaphore(device, &sci, nullptr, &compute_finished_) != VK_SUCCESS ||
        vkCreateSemaphore(device, &sci, nullptr, &graphics_to_compute_) != VK_SUCCESS)
        throw std::runtime_error("SobelCompute: semaphores");
}

void SobelCompute::create(const SobelComputeCreateInfo& info)
{
    if (!info.device || !info.mem_props || info.width == 0 || info.height == 0)
        throw std::runtime_error("SobelCompute::create: invalid args");

    device_ = info.device;
    width_ = info.width;
    height_ = info.height;
    graphics_family_ = info.graphics_family;
    compute_family_ = info.compute_family;

    create_edge_image(info);
    create_descriptors(info.device);
    create_compute_pipeline(info.device);
    create_overlay_pipeline(info.device, VK_FORMAT_R8G8B8A8_SRGB, info.depth_format);
    create_sync(info.device);
}

void SobelCompute::destroy(VkDevice device)
{
    if (!device)
        return;

    if (graphics_to_compute_ != VK_NULL_HANDLE)
    {
        vkDestroySemaphore(device, graphics_to_compute_, nullptr);
        graphics_to_compute_ = VK_NULL_HANDLE;
    }
    if (compute_finished_ != VK_NULL_HANDLE)
    {
        vkDestroySemaphore(device, compute_finished_, nullptr);
        compute_finished_ = VK_NULL_HANDLE;
    }
    if (overlay_pipeline_ != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(device, overlay_pipeline_, nullptr);
        overlay_pipeline_ = VK_NULL_HANDLE;
    }
    if (overlay_layout_ != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(device, overlay_layout_, nullptr);
        overlay_layout_ = VK_NULL_HANDLE;
    }
    if (pipeline_ != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(device, pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }
    if (compute_layout_ != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(device, compute_layout_, nullptr);
        compute_layout_ = VK_NULL_HANDLE;
    }
    if (descriptor_pool_ != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(device, descriptor_pool_, nullptr);
        descriptor_pool_ = VK_NULL_HANDLE;
        compute_set_ = VK_NULL_HANDLE;
        overlay_set_ = VK_NULL_HANDLE;
    }
    if (compute_set_layout_ != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(device, compute_set_layout_, nullptr);
        compute_set_layout_ = VK_NULL_HANDLE;
    }
    if (overlay_set_layout_ != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(device, overlay_set_layout_, nullptr);
        overlay_set_layout_ = VK_NULL_HANDLE;
    }
    if (depth_sampler_ != VK_NULL_HANDLE)
    {
        vkDestroySampler(device, depth_sampler_, nullptr);
        depth_sampler_ = VK_NULL_HANDLE;
    }
    if (edge_sampler_ != VK_NULL_HANDLE)
    {
        vkDestroySampler(device, edge_sampler_, nullptr);
        edge_sampler_ = VK_NULL_HANDLE;
    }
    if (edge_view_ != VK_NULL_HANDLE)
    {
        destroy_image_view(device, edge_view_);
        edge_view_ = VK_NULL_HANDLE;
    }
    if (edge_image_ != VK_NULL_HANDLE)
    {
        destroy_image(device, edge_image_, edge_memory_);
        edge_image_ = VK_NULL_HANDLE;
        edge_memory_ = VK_NULL_HANDLE;
    }
}

void SobelCompute::recreate(const SobelComputeCreateInfo& info)
{
    destroy(info.device);
    create(info);
}

void SobelCompute::record_dispatch(VkCommandBuffer cmd, VkImageView depth_view,
                                   float strength, float threshold)
{
    // Update compute descriptors for this frame's depth view
    VkDescriptorImageInfo depth_info{};
    depth_info.sampler = depth_sampler_;
    depth_info.imageView = depth_view;
    depth_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorImageInfo edge_info{};
    edge_info.imageView = edge_view_;
    edge_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSet writes[2]{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = compute_set_;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].descriptorCount = 1;
    writes[0].pImageInfo = &depth_info;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = compute_set_;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[1].descriptorCount = 1;
    writes[1].pImageInfo = &edge_info;
    vkUpdateDescriptorSets(device_, 2, writes, 0, nullptr);

    // Edge image → GENERAL for storage write
    {
        VkImageMemoryBarrier2 b{};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        b.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        b.srcAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        b.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        b.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        b.image = edge_image_;
        b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers = &b;
        vkCmdPipelineBarrier2(cmd, &dep);
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, compute_layout_, 0, 1,
                            &compute_set_, 0, nullptr);

    SobelPC pc{};
    pc.strength = strength;
    pc.threshold = threshold;
    pc.inv_width = 1.0f / static_cast<float>(width_);
    pc.inv_height = 1.0f / static_cast<float>(height_);
    vkCmdPushConstants(cmd, compute_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

    const uint32_t gx = (width_ + 7) / 8;
    const uint32_t gy = (height_ + 7) / 8;
    vkCmdDispatch(cmd, gx, gy, 1);

    // Edge image → SHADER_READ for overlay
    {
        VkImageMemoryBarrier2 b{};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        b.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        b.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        b.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        b.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        b.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.image = edge_image_;
        b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        if (graphics_family_ != compute_family_)
        {
            b.srcQueueFamilyIndex = compute_family_;
            b.dstQueueFamilyIndex = graphics_family_;
        }
        VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers = &b;
        vkCmdPipelineBarrier2(cmd, &dep);
    }
}

void SobelCompute::record_overlay(VkCommandBuffer cmd, uint32_t width, uint32_t height,
                                  const float highlight_rgba[4])
{
    // Rewrite overlay set in case edge view changed
    VkDescriptorImageInfo img{};
    img.sampler = edge_sampler_;
    img.imageView = edge_view_;
    img.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet w{};
    w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet = overlay_set_;
    w.dstBinding = 0;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.descriptorCount = 1;
    w.pImageInfo = &img;
    vkUpdateDescriptorSets(device_, 1, &w, 0, nullptr);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, overlay_pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, overlay_layout_, 0, 1,
                            &overlay_set_, 0, nullptr);

    VkViewport vp{};
    vp.width = static_cast<float>(width);
    vp.height = static_cast<float>(height);
    vp.minDepth = 0.f;
    vp.maxDepth = 1.f;
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D sc{ { 0, 0 }, { width, height } };
    vkCmdSetScissor(cmd, 0, 1, &sc);

    OverlayPC pc{};
    std::memcpy(pc.highlight, highlight_rgba, sizeof(pc.highlight));
    vkCmdPushConstants(cmd, overlay_layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);

    vkCmdDraw(cmd, 3, 1, 0, 0);
}
