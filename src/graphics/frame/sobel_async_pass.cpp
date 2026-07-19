#include "graphics/pch.h"
#include "graphics/frame/sobel_async_pass.hpp"
#include "graphics/frame/frame_presenter.hpp"
#include "graphics/frame/frame_recorder.hpp"
#include "graphics/frame/frame_sync.hpp"

#include <stdexcept>

void SobelAsyncPass::create(VkDevice device)
{
    if (done_fence_ != VK_NULL_HANDLE)
        return;
    VkFenceCreateInfo fci{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    if (vkCreateFence(device, &fci, nullptr, &done_fence_) != VK_SUCCESS)
        throw std::runtime_error("SobelAsyncPass: create fence failed");
    fence_in_flight_ = false;
}

void SobelAsyncPass::destroy(VkDevice device)
{
    if (done_fence_ != VK_NULL_HANDLE)
    {
        if (fence_in_flight_)
            vkWaitForFences(device, 1, &done_fence_, VK_TRUE, UINT64_MAX);
        vkDestroyFence(device, done_fence_, nullptr);
        done_fence_ = VK_NULL_HANDLE;
        fence_in_flight_ = false;
    }
}

void SobelAsyncPass::wait_idle(VkDevice device)
{
    if (fence_in_flight_ && done_fence_ != VK_NULL_HANDLE && device != VK_NULL_HANDLE)
    {
        vkWaitForFences(device, 1, &done_fence_, VK_TRUE, UINT64_MAX);
        vkResetFences(device, 1, &done_fence_);
        fence_in_flight_ = false;
    }
}

void SobelAsyncPass::submit(SobelPipeline& pipe,
                            const SobelAsyncSubmitContext& ctx,
                            uint32_t frame_index,
                            uint32_t image_index)
{
    if (ctx.outline_count == 0)
        throw std::runtime_error("async sobel: empty outline list");
    if (!ctx.device || !ctx.graphics_queue || !ctx.compute_queue || !ctx.recorder ||
        !ctx.frame_sync || !ctx.presenter || !ctx.outline_instance_buffer)
        throw std::runtime_error("async sobel: incomplete context");
    if (done_fence_ == VK_NULL_HANDLE)
        create(ctx.device);

    // Wait for prior Sobel chain: shared sel_depth/edge/color must not rewrite mid-flight.
    if (fence_in_flight_ && done_fence_ != VK_NULL_HANDLE)
    {
        vkWaitForFences(ctx.device, 1, &done_fence_, VK_TRUE, UINT64_MAX);
        vkResetFences(ctx.device, 1, &done_fence_);
        fence_in_flight_ = false;
    }

    VkCommandBuffer graphics_cb = ctx.main_graphics_cb;
    VkCommandBuffer compute_cb = ctx.compute_cb;
    VkCommandBuffer overlay_cb = ctx.overlay_cb;
    const VkSemaphore g2c = pipe.graphics_to_compute(frame_index);
    const VkSemaphore cfin = pipe.compute_finished(frame_index);

    // Single outline pass: all cubes, one clear, colors from compact instance buffer.
    OutlinePassDrawParams sp{};
    sp.cmd = graphics_cb;
    sp.ubo_set = ctx.frame_ubo_set;
    sp.vertex_buffer = ctx.vertex_buffer;
    sp.outline_instance_buffer = ctx.outline_instance_buffer;
    sp.index_buffer = ctx.index_buffer;
    sp.outline_count = ctx.outline_count;
    sp.index_count = ctx.index_count;
    sp.width = ctx.width;
    sp.height = ctx.height;
    pipe.record_outline_pass(sp);
    pipe.record_sel_depth_release_for_compute(graphics_cb);
    pipe.record_outline_color_to_shader_read(graphics_cb);
    ctx.recorder->end(graphics_cb);

    // Submit graphics (scene + pick + outline) → signal g2c.
    {
        VkCommandBufferSubmitInfo cb{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
        cb.commandBuffer = graphics_cb;
        VkSemaphoreSubmitInfo wait{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
        wait.semaphore = ctx.image_available;
        wait.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSemaphoreSubmitInfo sig{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
        sig.semaphore = g2c;
        sig.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        VkSubmitInfo2 submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
        submit.commandBufferInfoCount = 1;
        submit.pCommandBufferInfos = &cb;
        submit.waitSemaphoreInfoCount = 1;
        submit.pWaitSemaphoreInfos = &wait;
        submit.signalSemaphoreInfoCount = 1;
        submit.pSignalSemaphoreInfos = &sig;
        if (vkQueueSubmit2(ctx.graphics_queue, 1, &submit, VK_NULL_HANDLE) != VK_SUCCESS)
            throw std::runtime_error("async sobel: graphics submit failed");
    }

    // CMP: depth Sobel → white edge mask.
    {
        vkResetCommandBuffer(compute_cb, 0);
        VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(compute_cb, &bi);
        pipe.record_dispatch(compute_cb, /*strength=*/14.0f, /*threshold=*/0.001f);
        vkEndCommandBuffer(compute_cb);

        VkCommandBufferSubmitInfo cb{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
        cb.commandBuffer = compute_cb;
        VkSemaphoreSubmitInfo wait{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
        wait.semaphore = g2c;
        wait.stageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        VkSemaphoreSubmitInfo sig{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
        sig.semaphore = cfin;
        sig.stageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        VkSubmitInfo2 submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
        submit.commandBufferInfoCount = 1;
        submit.pCommandBufferInfos = &cb;
        submit.waitSemaphoreInfoCount = 1;
        submit.pWaitSemaphoreInfos = &wait;
        submit.signalSemaphoreInfoCount = 1;
        submit.pSignalSemaphoreInfos = &sig;
        if (vkQueueSubmit2(ctx.compute_queue, 1, &submit, VK_NULL_HANDLE) != VK_SUCCESS)
            throw std::runtime_error("async sobel: compute submit failed");
    }

    // Overlay: edge (white) × outline color → present.
    {
        vkResetCommandBuffer(overlay_cb, 0);
        VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(overlay_cb, &bi);
        pipe.record_edge_acquire_for_graphics(overlay_cb);

        VkRenderingAttachmentInfo color{};
        color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        color.imageView = ctx.swapchain_color_view;
        color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

        VkRenderingInfo ri{ VK_STRUCTURE_TYPE_RENDERING_INFO };
        ri.renderArea = { { 0, 0 }, { ctx.width, ctx.height } };
        ri.layerCount = 1;
        ri.colorAttachmentCount = 1;
        ri.pColorAttachments = &color;
        vkCmdBeginRendering(overlay_cb, &ri);
        pipe.record_overlay(overlay_cb, ctx.width, ctx.height, /*intensity=*/1.0f);
        vkCmdEndRendering(overlay_cb);

        ctx.recorder->transition_color_to_present(overlay_cb, ctx.swapchain_image);
        vkEndCommandBuffer(overlay_cb);

        const uint64_t signal_value = ctx.presenter->frame_counter() + 1;
        VkCommandBufferSubmitInfo ocb{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
        ocb.commandBuffer = overlay_cb;
        VkSemaphoreSubmitInfo wait{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
        wait.semaphore = cfin;
        wait.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT |
                         VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        VkSemaphoreSubmitInfo sigs[2]{};
        sigs[0].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        sigs[0].semaphore = ctx.frame_sync->timeline();
        sigs[0].value = signal_value;
        sigs[0].stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        sigs[1].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        sigs[1].semaphore = ctx.render_finished;
        sigs[1].stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo2 submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
        submit.commandBufferInfoCount = 1;
        submit.pCommandBufferInfos = &ocb;
        submit.waitSemaphoreInfoCount = 1;
        submit.pWaitSemaphoreInfos = &wait;
        submit.signalSemaphoreInfoCount = 2;
        submit.pSignalSemaphoreInfos = sigs;
        if (vkQueueSubmit2(ctx.graphics_queue, 1, &submit, done_fence_) != VK_SUCCESS)
            throw std::runtime_error("async sobel: overlay submit failed");
        fence_in_flight_ = true;

        VkPresentInfoKHR present{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &ctx.render_finished;
        present.swapchainCount = 1;
        present.pSwapchains = &ctx.swapchain;
        present.pImageIndices = &image_index;
        vkQueuePresentKHR(ctx.graphics_queue, &present);

        ctx.frame_sync->set_frame_timeline_value(frame_index, signal_value);
        ctx.presenter->notify_submitted_and_presented();
    }
}
