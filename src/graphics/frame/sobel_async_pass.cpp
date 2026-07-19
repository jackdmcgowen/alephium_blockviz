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
                            uint32_t image_index,
                            const SobelFrameRequest& req)
{
    if (req.layers.empty())
        throw std::runtime_error("async sobel: empty layers");
    if (!ctx.device || !ctx.graphics_queue || !ctx.compute_queue || !ctx.recorder ||
        !ctx.frame_sync || !ctx.presenter)
        throw std::runtime_error("async sobel: incomplete context");
    if (done_fence_ == VK_NULL_HANDLE)
        create(ctx.device);

    // Wait for prior Sobel chain: shared sel_depth/edge must not be rewritten while sampled.
    if (fence_in_flight_ && done_fence_ != VK_NULL_HANDLE)
    {
        vkWaitForFences(ctx.device, 1, &done_fence_, VK_TRUE, UINT64_MAX);
        vkResetFences(ctx.device, 1, &done_fence_);
        fence_in_flight_ = false;
    }

    VkCommandBuffer graphics_cb = ctx.main_graphics_cb;
    VkCommandBuffer compute_cb = ctx.compute_cb;
    VkCommandBuffer overlay_cb = ctx.overlay_cb;
    VkCommandBuffer layer_depth_cb = ctx.layer_depth_cb;
    const VkSemaphore g2c = pipe.graphics_to_compute(frame_index);
    const VkSemaphore cfin = pipe.compute_finished(frame_index);

    // Role accents for dark brand canvas (docs/brand/alephium_palette.md).
    static const float kGoldHighlight[4]   = { 0.94f, 0.76f, 0.29f, 1.35f };
    static const float kGreenHighlight[4]  = { 0.24f, 0.86f, 0.52f, 1.35f };
    static const float kCyanHighlight[4]   = { 0.18f, 0.90f, 0.94f, 1.35f };
    static const float kOrangeHighlight[4] = { 1.00f, 0.54f, 0.12f, 1.35f };

    auto highlight_for = [&](SobelFrameRequest::Mode mode) -> const float* {
        if (mode == SobelFrameRequest::Mode::ConfirmedTipsGreen)
            return kGreenHighlight;
        if (mode == SobelFrameRequest::Mode::CyanFrontier)
            return kCyanHighlight;
        if (mode == SobelFrameRequest::Mode::IncompleteTraceOrange)
            return kOrangeHighlight;
        return kGoldHighlight;
    };

    auto record_depth = [&](VkCommandBuffer cmd, const SobelFrameRequest::Layer& layer) {
        SelectionDepthDrawParams sp{};
        sp.cmd = cmd;
        sp.ubo_set = ctx.frame_ubo_set;
        sp.vertex_buffer = ctx.vertex_buffer;
        sp.instance_buffer = ctx.instance_buffer;
        sp.index_buffer = ctx.index_buffer;
        sp.instance_indices = layer.instance_indices.data();
        sp.instance_index_count = static_cast<uint32_t>(layer.instance_indices.size());
        sp.index_count = ctx.index_count;
        sp.width = ctx.width;
        sp.height = ctx.height;
        pipe.record_selection_depth(sp);
        pipe.record_sel_depth_release_for_compute(cmd);
    };

    // Layer 0 depth on main graphics CB (already has scene + pick).
    record_depth(graphics_cb, req.layers[0]);
    ctx.recorder->end(graphics_cb);

    auto wait_fence = [&]() {
        vkWaitForFences(ctx.device, 1, &done_fence_, VK_TRUE, UINT64_MAX);
        vkResetFences(ctx.device, 1, &done_fence_);
        fence_in_flight_ = false;
    };

    auto record_and_submit_compute = [&](bool wait_g2c, bool signal_cfin) {
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
        if (wait_g2c)
        {
            submit.waitSemaphoreInfoCount = 1;
            submit.pWaitSemaphoreInfos = &wait;
        }
        if (signal_cfin)
        {
            submit.signalSemaphoreInfoCount = 1;
            submit.pSignalSemaphoreInfos = &sig;
        }
        VkFence fence = signal_cfin ? VK_NULL_HANDLE : done_fence_;
        if (vkQueueSubmit2(ctx.compute_queue, 1, &submit, fence) != VK_SUCCESS)
            throw std::runtime_error("async sobel: compute submit failed");
        if (fence != VK_NULL_HANDLE)
        {
            wait_fence();
            fence_in_flight_ = false;
        }
    };

    auto record_overlay_cb = [&](const SobelFrameRequest::Layer& layer, bool present_ready) {
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
        pipe.record_overlay(overlay_cb, ctx.width, ctx.height, highlight_for(layer.mode));
        vkCmdEndRendering(overlay_cb);

        if (present_ready)
            ctx.recorder->transition_color_to_present(overlay_cb, ctx.swapchain_image);
        vkEndCommandBuffer(overlay_cb);
    };

    for (size_t li = 0; li < req.layers.size(); ++li)
    {
        const SobelFrameRequest::Layer& layer = req.layers[li];
        const bool first = (li == 0);
        const bool last = (li + 1 == req.layers.size());

        if (first)
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

            record_and_submit_compute(/*wait_g2c=*/true, /*signal_cfin=*/true);
        }
        else
        {
            vkResetCommandBuffer(layer_depth_cb, 0);
            VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
            bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vkBeginCommandBuffer(layer_depth_cb, &bi);
            record_depth(layer_depth_cb, layer);
            vkEndCommandBuffer(layer_depth_cb);

            VkCommandBufferSubmitInfo cb{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
            cb.commandBuffer = layer_depth_cb;
            VkSubmitInfo2 submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
            submit.commandBufferInfoCount = 1;
            submit.pCommandBufferInfos = &cb;
            if (vkQueueSubmit2(ctx.graphics_queue, 1, &submit, done_fence_) != VK_SUCCESS)
                throw std::runtime_error("async sobel: layer depth submit failed");
            wait_fence();

            record_and_submit_compute(/*wait_g2c=*/false, /*signal_cfin=*/false);
        }

        record_overlay_cb(layer, /*present_ready=*/last);

        VkCommandBufferSubmitInfo ocb{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
        ocb.commandBuffer = overlay_cb;

        if (first)
        {
            VkSemaphoreSubmitInfo waits[1]{};
            waits[0].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            waits[0].semaphore = cfin;
            waits[0].stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT |
                                 VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;

            if (last)
            {
                const uint64_t signal_value = ctx.presenter->frame_counter() + 1;
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
                submit.pWaitSemaphoreInfos = waits;
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
            else
            {
                VkSubmitInfo2 submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
                submit.commandBufferInfoCount = 1;
                submit.pCommandBufferInfos = &ocb;
                submit.waitSemaphoreInfoCount = 1;
                submit.pWaitSemaphoreInfos = waits;
                if (vkQueueSubmit2(ctx.graphics_queue, 1, &submit, done_fence_) != VK_SUCCESS)
                    throw std::runtime_error("async sobel: intermediate overlay submit failed");
                wait_fence();
            }
        }
        else if (last)
        {
            const uint64_t signal_value = ctx.presenter->frame_counter() + 1;
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
            submit.signalSemaphoreInfoCount = 2;
            submit.pSignalSemaphoreInfos = sigs;
            if (vkQueueSubmit2(ctx.graphics_queue, 1, &submit, done_fence_) != VK_SUCCESS)
                throw std::runtime_error("async sobel: final multi-layer overlay failed");
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
        else
        {
            VkSubmitInfo2 submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
            submit.commandBufferInfoCount = 1;
            submit.pCommandBufferInfos = &ocb;
            if (vkQueueSubmit2(ctx.graphics_queue, 1, &submit, done_fence_) != VK_SUCCESS)
                throw std::runtime_error("async sobel: mid multi-layer overlay failed");
            wait_fence();
        }
    }
}
