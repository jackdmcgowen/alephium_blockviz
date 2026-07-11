#include "engine/frame_recorder.hpp"

#include "graphics/debug/debug_drawer.h"
#include "graphics/mesh_arena.h"

#include "imgui.h"
#include "imgui_impl_vulkan.h"

#include <stdexcept>

static void pipeline_barrier(VkCommandBuffer buffer, VkImage image,
    VkImageLayout oldLayout, VkAccessFlags2 srcAccessMask, VkPipelineStageFlags2 srcStageMask,
    VkImageLayout newLayout, VkAccessFlags2 dstAccessMask, VkPipelineStageFlags2 dstStageMask,
    VkImageSubresourceRange subresourceRange)
{
    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcAccessMask = srcAccessMask;
    barrier.srcStageMask = srcStageMask;
    barrier.dstAccessMask = dstAccessMask;
    barrier.dstStageMask = dstStageMask;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = subresourceRange;

    VkDependencyInfo depInfo{};
    depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    depInfo.imageMemoryBarrierCount = 1;
    depInfo.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(buffer, &depInfo);
}

void FrameRecorder::record_main(const FrameRecordParams& p)
{
    if (!p.cmd)
        throw std::runtime_error("FrameRecorder::record_main: null command buffer");

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(p.cmd, &beginInfo) != VK_SUCCESS)
        throw std::runtime_error("Failed to begin recording command buffer");

    const bool msaa = p.samples > VK_SAMPLE_COUNT_1_BIT && p.resolve_color_view != VK_NULL_HANDLE;

    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = p.color_view;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = msaa ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color = { { 0.7f, 0.7f, 0.7f, 1.0f } };
    if (msaa)
    {
        colorAttachment.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
        colorAttachment.resolveImageView = p.resolve_color_view;
        colorAttachment.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    VkRenderingAttachmentInfo depthAttachment{};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = p.depth_view;
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.clearValue.depthStencil = { 1.0f, 0 };

    VkRenderingInfo renderInfo{};
    renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderInfo.renderArea = { { 0, 0 }, { p.width, p.height } };
    renderInfo.layerCount = 1;
    renderInfo.colorAttachmentCount = 1;
    renderInfo.pColorAttachments = &colorAttachment;
    renderInfo.pDepthAttachment = &depthAttachment;

    // Barriers for MSAA color / resolve / depth
    if (p.after_resize || msaa)
    {
        if (msaa && p.color_image != VK_NULL_HANDLE)
        {
            pipeline_barrier(p.cmd, p.color_image,
                VK_IMAGE_LAYOUT_UNDEFINED, 0, 0,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });
        }
        if (p.resolve_color_image != VK_NULL_HANDLE || (!msaa && p.color_image != VK_NULL_HANDLE))
        {
            VkImage img = msaa ? p.resolve_color_image : p.color_image;
            pipeline_barrier(p.cmd, img,
                VK_IMAGE_LAYOUT_UNDEFINED, 0, 0,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });
        }
        pipeline_barrier(p.cmd, p.depth_image,
            VK_IMAGE_LAYOUT_UNDEFINED, 0, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
            { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 });
    }

    // --- Pass A: scene (cubes + debug) into MSAA or 1× color ---
    vkCmdBeginRendering(p.cmd, &renderInfo);
    vkCmdBindPipeline(p.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p.cube_pipeline);
    vkCmdSetPrimitiveTopology(p.cmd, p.topology);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(p.width);
    viewport.height = static_cast<float>(p.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(p.cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = p.scissor_extent;
    vkCmdSetScissor(p.cmd, 0, 1, &scissor);

    VkBuffer buffers[] = { p.vertex_buffer, p.instance_buffer };
    VkDeviceSize offsets[] = { 0, 0 };
    vkCmdBindVertexBuffers(p.cmd, 0, 2, buffers, offsets);
    VkDescriptorSet set = p.descriptor_set;
    vkCmdBindDescriptorSets(p.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p.cube_layout, 0, 1, &set, 0, nullptr);
    vkCmdBindIndexBuffer(p.cmd, p.index_buffer, 0, VK_INDEX_TYPE_UINT16);
    vkCmdDrawIndexed(p.cmd, p.index_count, p.instance_count, 0, 0, 0);

    if (p.mesh_arena && p.debug_drawer && p.view_proj)
    {
        p.mesh_arena->upload(*p.debug_drawer);
        p.mesh_arena->draw(p.cmd, *p.view_proj);
    }

    vkCmdEndRendering(p.cmd);

    // --- Pass B: ImGui on resolved 1× swapchain color (after MSAA resolve) ---
    if (p.imgui_draw_data)
    {
        VkImageView ui_view = msaa ? p.resolve_color_view : p.color_view;
        VkRenderingAttachmentInfo uiColor{};
        uiColor.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        uiColor.imageView = ui_view;
        uiColor.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        uiColor.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        uiColor.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

        VkRenderingInfo uiInfo{};
        uiInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        uiInfo.renderArea = { { 0, 0 }, { p.width, p.height } };
        uiInfo.layerCount = 1;
        uiInfo.colorAttachmentCount = 1;
        uiInfo.pColorAttachments = &uiColor;
        vkCmdBeginRendering(p.cmd, &uiInfo);
        vkCmdSetViewport(p.cmd, 0, 1, &viewport);
        vkCmdSetScissor(p.cmd, 0, 1, &scissor);
        ImGui_ImplVulkan_RenderDrawData(p.imgui_draw_data, p.cmd);
        vkCmdEndRendering(p.cmd);
    }

    if (p.transition_color_to_present)
    {
        VkImage present_img = msaa ? p.resolve_color_image : p.color_image;
        transition_color_to_present(p.cmd, present_img);
    }
}

void FrameRecorder::transition_color_to_present(VkCommandBuffer cmd, VkImage color_image)
{
    pipeline_barrier(cmd, color_image,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, 0, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
        { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });
}

void FrameRecorder::end(VkCommandBuffer cmd)
{
    if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
        throw std::runtime_error("Failed to record command buffer");
}
