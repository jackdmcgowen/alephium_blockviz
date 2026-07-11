#include "engine/picker.hpp"

#include "graphics/gpu_prv_lib.h"

#include <cstring>
#include <stdexcept>
#include <vector>

static void pipeline_barrier(VkCommandBuffer buffer, VkImage image,
    VkImageLayout oldLayout, VkAccessFlags2 srcAccessMask, VkPipelineStageFlags2 srcStageMask,
    VkImageLayout newLayout, VkAccessFlags2 dstAccessMask, VkPipelineStageFlags2 dstStageMask,
    VkImageSubresourceRange subresourceRange)
{
    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask = srcStageMask;
    barrier.srcAccessMask = srcAccessMask;
    barrier.dstStageMask = dstStageMask;
    barrier.dstAccessMask = dstAccessMask;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = subresourceRange;

    VkDependencyInfo dep{};
    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(buffer, &dep);
}

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
}

void Picker::recreate_resources(const PickerResourcesCreateInfo& info)
{
    destroy_resources(info.device);
    create_resources(info);
}

void Picker::create_staging(VkDevice device, VkPhysicalDeviceMemoryProperties* mem_props)
{
    if (!device || !mem_props)
        throw std::runtime_error("Picker::create_staging: invalid args");

    create_buffer(
        device, mem_props,
        kPickerReadExtent.width * kPickerReadExtent.height * sizeof(uint32_t),
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        staging_buffer_,
        staging_memory_);
}

void Picker::destroy_staging(VkDevice device)
{
    if (!device)
        return;
    if (staging_buffer_ != VK_NULL_HANDLE)
    {
        destroy_buffer(device, staging_buffer_, staging_memory_);
        staging_buffer_ = VK_NULL_HANDLE;
        staging_memory_ = VK_NULL_HANDLE;
    }
}

void Picker::destroy(VkDevice device)
{
    destroy_resources(device);
    destroy_staging(device);
}

void Picker::record_pass(const PickerRecordParams& p)
{
    if (!p.cmd || image_ == VK_NULL_HANDLE || image_view_ == VK_NULL_HANDLE)
        throw std::runtime_error("Picker::record_pass: not ready");

    if (p.image_layout_undefined)
    {
        pipeline_barrier(p.cmd, image_,
            VK_IMAGE_LAYOUT_UNDEFINED, 0, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });
    }
    else
    {
        pipeline_barrier(p.cmd, image_,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_2_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });
    }

    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = image_view_;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color.uint32[0] = kPickerInvalidId;

    VkRenderingAttachmentInfo depthAttachment{};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = p.depth_view;
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
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

    pipeline_barrier(p.cmd, image_,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_2_TRANSFER_READ_BIT,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT,
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
        staging_buffer_,
        1, &copyRegion);
}

uint32_t Picker::read_object_id(VkDevice device) const
{
    if (!device || staging_memory_ == VK_NULL_HANDLE)
        return kPickerInvalidId;

    uint32_t* ptr = nullptr;
    std::vector<uint32_t> id(kPickerReadExtent.width * kPickerReadExtent.height);

    vkMapMemory(device, staging_memory_, 0, VK_WHOLE_SIZE, 0, reinterpret_cast<void**>(&ptr));
    std::memcpy(id.data(), ptr, kPickerReadExtent.width * kPickerReadExtent.height * sizeof(uint32_t));
    vkUnmapMemory(device, staging_memory_);

    return (id[0] == kPickerInvalidId) ? kPickerInvalidId : id[0];
}
