#include "graphics/pch.h"
#include "graphics/frame/passes/picker_pass.hpp"
#include "graphics/frame/profiling/frame_profiler.hpp"
#include "graphics/gpu_prv_lib.h"

#include <cstring>
#include <stdexcept>
#include <vector>

namespace frame_graph
{

void PickerPass::create(const PassCreateInfo& info)
{
    if (!info.device || !info.mem_props || info.width == 0 || info.height == 0)
        throw std::runtime_error("PickerPass::create: invalid args");
    if (!info.frame_ubo_layout)
        throw std::runtime_error("PickerPass::create: frame_ubo_layout required");

    create_resources_(info);
    create_staging_(info.buffers);
    create_pipeline_(info);
}

void PickerPass::destroy(VkDevice device)
{
    destroy_pipeline_(device);
    destroy_resources_(device);
    destroy_staging_();
}

void PickerPass::recreate(const PassCreateInfo& info)
{
    // Staging is size-fixed (1×1); only images + PSO need rebuild on resize.
    destroy_pipeline_(info.device);
    destroy_resources_(info.device);
    create_resources_(info);
    create_pipeline_(info);
    if (!staging_.valid() && info.buffers)
        create_staging_(info.buffers);
}

void PickerPass::declare_resources(std::vector<ResourceId>& /*reads*/,
                                   std::vector<ResourceId>& writes) const
{
    writes.push_back(ResourceId::PickerColor);
}

void PickerPass::create_resources_(const PassCreateInfo& info)
{
    destroy_resources_(info.device);

    create_image(
        info.device,
        info.width, info.height,
        kPickerColorFormat,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        image_,
        memory_,
        info.mem_props);

    image_view_ = create_image_view(info.device, image_, kPickerColorFormat, VK_IMAGE_ASPECT_COLOR_BIT);

    // Always 1× depth (matches 1× color + 1× picker pipeline).
    const VkFormat depth_fmt =
        (info.depth_format != VK_FORMAT_UNDEFINED) ? info.depth_format : VK_FORMAT_D32_SFLOAT;
    create_image(
        info.device,
        info.width, info.height,
        depth_fmt,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        depth_image_,
        depth_memory_,
        info.mem_props);
    depth_view_ = create_image_view(info.device, depth_image_, depth_fmt, VK_IMAGE_ASPECT_DEPTH_BIT);
}

void PickerPass::destroy_resources_(VkDevice device)
{
    if (!device)
        return;
    if (image_view_ != VK_NULL_HANDLE)
    {
        destroy_image_view(device, image_view_);
        image_view_ = VK_NULL_HANDLE;
    }
    if (image_ != VK_NULL_HANDLE)
    {
        destroy_image(device, image_, memory_);
        image_ = VK_NULL_HANDLE;
        memory_ = VK_NULL_HANDLE;
    }
    if (depth_view_ != VK_NULL_HANDLE)
    {
        destroy_image_view(device, depth_view_);
        depth_view_ = VK_NULL_HANDLE;
    }
    if (depth_image_ != VK_NULL_HANDLE)
    {
        destroy_image(device, depth_image_, depth_memory_);
        depth_image_ = VK_NULL_HANDLE;
        depth_memory_ = VK_NULL_HANDLE;
    }
}

void PickerPass::create_staging_(BufferManager* buffers)
{
    if (!buffers)
        throw std::runtime_error("PickerPass::create_staging: null BufferManager");
    destroy_staging_();
    buffers_ = buffers;
    staging_ = buffers_->create(BufferDesc{
        kPickerReadExtent.width * kPickerReadExtent.height * sizeof(uint32_t),
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        "picker.staging"});
}

void PickerPass::destroy_staging_()
{
    if (buffers_ && staging_.valid())
        buffers_->destroy(staging_);
    buffers_ = nullptr;
}

void PickerPass::create_pipeline_(const PassCreateInfo& info)
{
    destroy_pipeline_(info.device);

    std::vector<uint8_t> vertShaderCode;
    std::vector<uint8_t> fragShaderCode;
    load_shader_source("picker_vert.spv", vertShaderCode);
    load_shader_source("picker_frag.spv", fragShaderCode);

    VkShaderModule vertShaderModule;
    VkShaderModule fragShaderModule;
    create_shader_module(info.device, vertShaderModule, vertShaderCode);
    create_shader_module(info.device, fragShaderModule, fragShaderCode);

    VkPipelineShaderStageCreateInfo vertStageInfo{};
    vertStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStageInfo.module = vertShaderModule;
    vertStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragStageInfo{};
    fragStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStageInfo.module = fragShaderModule;
    fragStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertStageInfo, fragStageInfo };

    VkVertexInputBindingDescription bindingDescriptions[2];
    bindingDescriptions[0].binding = 0;
    bindingDescriptions[0].stride = sizeof(VertexNormal);
    bindingDescriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    bindingDescriptions[1].binding = 1;
    bindingDescriptions[1].stride = sizeof(InstanceData);
    bindingDescriptions[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    VkVertexInputAttributeDescription attributeDescriptions[2];
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(VertexNormal, pos.x);
    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[1].offset = offsetof(VertexNormal, normal.x);

    VkVertexInputAttributeDescription instanceAttributes[4];
    instanceAttributes[0].binding = 1;
    instanceAttributes[0].location = 2;
    instanceAttributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    instanceAttributes[0].offset = offsetof(InstanceData, pos);
    instanceAttributes[1].binding = 1;
    instanceAttributes[1].location = 3;
    instanceAttributes[1].format = VK_FORMAT_R32_SFLOAT;
    instanceAttributes[1].offset = offsetof(InstanceData, scale);
    instanceAttributes[2].binding = 1;
    instanceAttributes[2].location = 4;
    instanceAttributes[2].format = VK_FORMAT_R32G32B32_SFLOAT;
    instanceAttributes[2].offset = offsetof(InstanceData, color);
    instanceAttributes[3].binding = 1;
    instanceAttributes[3].location = 5;
    instanceAttributes[3].format = VK_FORMAT_R32_SFLOAT;
    instanceAttributes[3].offset = offsetof(InstanceData, alpha);

    VkVertexInputAttributeDescription attributes[] = {
        attributeDescriptions[0], attributeDescriptions[1],
        instanceAttributes[0], instanceAttributes[1],
        instanceAttributes[2], instanceAttributes[3]
    };

    VkPushConstantRange range{};
    range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    range.offset = 0;
    range.size = sizeof(PickerPushConstants);

    layout_ = create_pipeline_layout(info.device, &info.frame_ubo_layout, 1, &range, 1);

    GraphicsPipelineCreateInfo ginfo{};
    ginfo.layout = layout_;
    ginfo.stages = shaderStages;
    ginfo.stage_count = 2;
    ginfo.bindings = bindingDescriptions;
    ginfo.binding_count = 2;
    ginfo.attributes = attributes;
    ginfo.attribute_count = 6;
    ginfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    ginfo.cull_mode = VK_CULL_MODE_BACK_BIT;
    ginfo.front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    ginfo.depth_test = true;
    ginfo.depth_write = false;
    ginfo.depth_compare = VK_COMPARE_OP_LESS_OR_EQUAL;
    ginfo.blend_mode = PipelineBlendMode::None;
    ginfo.color_write_mask = VK_COLOR_COMPONENT_R_BIT;
    ginfo.samples = VK_SAMPLE_COUNT_1_BIT; // pick is always 1×
    ginfo.color_format = kPickerColorFormat;
    ginfo.depth_format =
        (info.depth_format != VK_FORMAT_UNDEFINED) ? info.depth_format : VK_FORMAT_D32_SFLOAT;
    ginfo.color_attachment_count = 1;
    ginfo.viewport_width = info.width;
    ginfo.viewport_height = info.height;
    ginfo.dynamic_viewport_scissor = true;
    ginfo.dynamic_primitive_topology = true;

    try
    {
        pipeline_ = create_graphics_pipeline(info.device, ginfo);
    }
    catch (...)
    {
        destroy_shader_module(info.device, fragShaderModule);
        destroy_shader_module(info.device, vertShaderModule);
        destroy_pipeline_(info.device);
        throw;
    }

    destroy_shader_module(info.device, fragShaderModule);
    destroy_shader_module(info.device, vertShaderModule);
}

void PickerPass::destroy_pipeline_(VkDevice device)
{
    if (device == VK_NULL_HANDLE)
        return;
    destroy_pipeline(device, pipeline_);
    pipeline_ = VK_NULL_HANDLE;
    destroy_pipeline_layout(device, layout_);
    layout_ = VK_NULL_HANDLE;
}

void PickerPass::record(const PassRecordParams& p)
{
    if (!p.base.cmd || pipeline_ == VK_NULL_HANDLE || image_ == VK_NULL_HANDLE)
        throw std::runtime_error("PickerPass::record: not ready");

    PassProfileScope profile(*this, p);
    const VkCommandBuffer cmd = p.base.cmd;

    if (p.picker_image_undefined)
    {
        cmd_image_barrier(cmd, image_,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            0, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });
    }
    else
    {
        cmd_image_barrier(cmd, image_,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_ACCESS_2_TRANSFER_READ_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });
    }

    // Private 1× depth always transitions from UNDEFINED each pick (cleared below).
    cmd_image_barrier(cmd, depth_image_,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        0, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
        { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 });

    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = image_view_;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color.uint32[0] = kPickerInvalidId;

    VkRenderingAttachmentInfo depthAttachment{};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = depth_view_;
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.clearValue.depthStencil = { 1.0f, 0 };

    VkRenderingInfo renderInfo{};
    renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderInfo.renderArea = { { 0, 0 }, { p.width, p.height } };
    renderInfo.layerCount = 1;
    renderInfo.colorAttachmentCount = 1;
    renderInfo.pColorAttachments = &colorAttachment;
    renderInfo.pDepthAttachment = &depthAttachment;

    vkCmdBeginRendering(cmd, &renderInfo);

    VkRect2D scissor{};
    scissor.offset = { static_cast<int32_t>(p.mouse_x), static_cast<int32_t>(p.mouse_y) };
    scissor.extent = { 1, 1 };
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    VkViewport vp{};
    vp.x = 0;
    vp.y = 0;
    vp.width = static_cast<float>(p.scissor_extent.width ? p.scissor_extent.width : p.width);
    vp.height = static_cast<float>(p.scissor_extent.height ? p.scissor_extent.height : p.height);
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &vp);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdSetPrimitiveTopology(cmd, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

    PickerPushConstants pc{};
    pc.mouseX = p.mouse_x;
    pc.mouseY = p.mouse_y;
    pc.instanceOffset = 0;

    vkCmdPushConstants(cmd, layout_,
        VK_SHADER_STAGE_FRAGMENT_BIT,
        0, sizeof(PickerPushConstants), &pc);

    VkBuffer buffers[] = { p.vertex_buffer, p.instance_buffer };
    VkDeviceSize offsets[] = { 0, 0 };
    vkCmdBindVertexBuffers(cmd, 0, 2, buffers, offsets);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_,
                            0, 1, &p.frame_ubo_set, 0, nullptr);
    vkCmdBindIndexBuffer(cmd, p.index_buffer, 0, VK_INDEX_TYPE_UINT16);
    vkCmdDrawIndexed(cmd, p.index_count, p.instance_count, 0, 0, 0);

    vkCmdEndRendering(cmd);

    cmd_image_barrier(cmd, image_,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });

    VkBufferImageCopy copyRegion{};
    copyRegion.bufferOffset = 0;
    copyRegion.bufferRowLength = 0;
    copyRegion.bufferImageHeight = 0;
    copyRegion.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    copyRegion.imageOffset = { static_cast<int32_t>(p.mouse_x), static_cast<int32_t>(p.mouse_y), 0 };
    copyRegion.imageExtent = { kPickerReadExtent.width, kPickerReadExtent.height, 1 };

    vkCmdCopyImageToBuffer(cmd,
        image_,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        staging_.handle(),
        1, &copyRegion);
}

uint32_t PickerPass::read_object_id(VkDevice device) const
{
    if (!device || !staging_.valid())
        return kPickerInvalidId;

    uint32_t* ptr = nullptr;
    std::vector<uint32_t> id(kPickerReadExtent.width * kPickerReadExtent.height);

    vkMapMemory(device, staging_.memory(), 0, VK_WHOLE_SIZE, 0, reinterpret_cast<void**>(&ptr));
    std::memcpy(id.data(), ptr, kPickerReadExtent.width * kPickerReadExtent.height * sizeof(uint32_t));
    vkUnmapMemory(device, staging_.memory());

    return (id[0] == kPickerInvalidId) ? kPickerInvalidId : id[0];
}

} // namespace frame_graph
