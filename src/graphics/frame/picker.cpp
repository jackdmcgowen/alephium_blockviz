#include "graphics/pch.h"
#include "graphics/frame/picker.hpp"

#include "graphics/gpu_prv_lib.h"

#include <cstring>
#include <stdexcept>
#include <vector>

void Picker::create_resources(const PickerResourcesCreateInfo& info)
{
    if (!info.device || !info.mem_props || info.width == 0 || info.height == 0)
        throw std::runtime_error("Picker::create_resources: invalid args");

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

    // Always 1Ã— depth (matches 1Ã— color + 1Ã— picker pipeline).
    create_image(
        info.device,
        info.width, info.height,
        info.depth_format,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        depth_image_,
        depth_memory_,
        info.mem_props);
    depth_view_ = create_image_view(info.device, depth_image_, info.depth_format,
                                    VK_IMAGE_ASPECT_DEPTH_BIT);
}

void Picker::destroy_resources(VkDevice device)
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

void Picker::recreate_resources(const PickerResourcesCreateInfo& info)
{
    destroy_resources(info.device);
    create_resources(info);
}

void Picker::create_staging(BufferManager* buffers)
{
    if (!buffers)
        throw std::runtime_error("Picker::create_staging: null BufferManager");
    destroy_staging();
    buffers_ = buffers;
    staging_ = buffers_->create(BufferDesc{
        kPickerReadExtent.width * kPickerReadExtent.height * sizeof(uint32_t),
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        "picker.staging"});
}

void Picker::destroy_staging()
{
    if (buffers_ && staging_.valid())
        buffers_->destroy(staging_);
    buffers_ = nullptr;
}

void Picker::destroy(VkDevice device)
{
    destroy_resources(device);
    destroy_staging();
}

void Picker::record_pass(const PickerRecordParams& p)
{
    if (!p.cmd || image_ == VK_NULL_HANDLE || image_view_ == VK_NULL_HANDLE)
        throw std::runtime_error("Picker::record_pass: not ready");

    if (p.image_layout_undefined)
    {
        cmd_image_barrier(p.cmd, image_,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            0, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });
    }
    else
    {
        cmd_image_barrier(p.cmd, image_,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_ACCESS_2_TRANSFER_READ_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });
    }

    // Private 1× depth always transitions from UNDEFINED each pick (cleared below).
    cmd_image_barrier(p.cmd, depth_image_,
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

    vkCmdBeginRendering(p.cmd, &renderInfo);

    VkRect2D scissor{};
    scissor.offset = { static_cast<int32_t>(p.mouse_x), static_cast<int32_t>(p.mouse_y) };
    scissor.extent = { 1, 1 };
    vkCmdSetScissor(p.cmd, 0, 1, &scissor);

    VkViewport vp{};
    vp.x = 0;
    vp.y = 0;
    vp.width = static_cast<float>(p.viewport_extent.width);
    vp.height = static_cast<float>(p.viewport_extent.height);
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(p.cmd, 0, 1, &vp);

    vkCmdBindPipeline(p.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p.pipeline);
    vkCmdSetPrimitiveTopology(p.cmd, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

    PickerPushConstants pc{};
    pc.mouseX = p.mouse_x;
    pc.mouseY = p.mouse_y;
    pc.instanceOffset = p.instance_offset;

    vkCmdPushConstants(p.cmd, p.pipeline_layout,
        VK_SHADER_STAGE_FRAGMENT_BIT,
        0, sizeof(PickerPushConstants), &pc);

    VkBuffer buffers[] = { p.vertex_buffer, p.instance_buffer };
    VkDeviceSize offsets[] = { 0, 0 };
    vkCmdBindVertexBuffers(p.cmd, 0, 2, buffers, offsets);
    vkCmdBindDescriptorSets(p.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p.pipeline_layout,
                            0, 1, &p.descriptor_set, 0, nullptr);
    vkCmdBindIndexBuffer(p.cmd, p.index_buffer, 0, VK_INDEX_TYPE_UINT16);
    vkCmdDrawIndexed(p.cmd, p.index_count, p.instance_count, 0, 0, 0);

    vkCmdEndRendering(p.cmd);

    cmd_image_barrier(p.cmd, image_,
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

    vkCmdCopyImageToBuffer(p.cmd,
        image_,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        staging_.handle(),
        1, &copyRegion);
}

uint32_t Picker::read_object_id(VkDevice device) const
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
