#include "graphics/pch.h"
#include "graphics/graphics_system.hpp"

#include <stdexcept>
#include <vector>
#include <cstring>

void GraphicsSystem::submit_frame_with_async_sobel(uint32_t frame_index, uint32_t image_index,
                                                 VkCommandBuffer graphics_cb,
                                                 VkSemaphore image_available,
                                                 VkSemaphore render_finished,
                                                 const SobelFrameRequest& req)
{
    if (req.layers.empty())
        throw std::runtime_error("async sobel: empty layers");

    // Wait for prior Sobel chain: shared sel_depth/edge must not be rewritten while sampled.
    if (sobel_fence_in_flight_ && sobel_done_fence_ != VK_NULL_HANDLE)
    {
        vkWaitForFences(device, 1, &sobel_done_fence_, VK_TRUE, UINT64_MAX);
        vkResetFences(device, 1, &sobel_done_fence_);
        sobel_fence_in_flight_ = false;
    }

    FramesInFlight& slot = inFlightFrames[frame_index % MAX_FRAMES_IN_FLIGHT];
    VkCommandBuffer compute_cb = slot.computeCommandBuffer;
    VkCommandBuffer overlay_cb = slot.overlayCommandBuffer;
    VkCommandBuffer layer_depth_cb = slot.layerDepthCommandBuffer;
    const VkSemaphore g2c = sobel_.graphics_to_compute(frame_index);
    const VkSemaphore cfin = sobel_.compute_finished(frame_index);

    static const float kGoldHighlight[4]   = { 1.0f, 0.85f, 0.15f, 1.35f };
    static const float kGreenHighlight[4]  = { 0.25f, 0.95f, 0.40f, 1.35f };
    static const float kCyanHighlight[4]   = { 0.15f, 0.95f, 1.0f, 1.35f };
    static const float kOrangeHighlight[4] = { 1.0f, 0.45f, 0.08f, 1.35f };

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
        sp.ubo_set = frame_descriptors_.set();
        sp.vertex_buffer = frame_resources_.vertex_buffer();
        sp.instance_buffer = frame_resources_.instance_buffer();
        sp.index_buffer = frame_resources_.index_buffer();
        sp.instance_indices = layer.instance_indices.data();
        sp.instance_index_count = static_cast<uint32_t>(layer.instance_indices.size());
        sp.width = width;
        sp.height = height;
        sobel_.record_selection_depth(sp);
        sobel_.record_sel_depth_release_for_compute(cmd);
    };

    // Layer 0 depth on main graphics CB (already has scene + pick).
    record_depth(graphics_cb, req.layers[0]);
    frame_recorder_.end(graphics_cb);

    // Layer 0 uses g2c/cfin once. Extra layers are fence-only (no binary re-signal).
    auto wait_fence = [&]() {
        vkWaitForFences(device, 1, &sobel_done_fence_, VK_TRUE, UINT64_MAX);
        vkResetFences(device, 1, &sobel_done_fence_);
        sobel_fence_in_flight_ = false;
    };

    auto record_and_submit_compute = [&](bool wait_g2c, bool signal_cfin) {
        vkResetCommandBuffer(compute_cb, 0);
        VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(compute_cb, &bi);
        sobel_.record_dispatch(compute_cb, /*strength=*/14.0f, /*threshold=*/0.001f);
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
        // Fence when not using cfin (extra layers) so we can CPU-sync before overlay.
        VkFence fence = signal_cfin ? VK_NULL_HANDLE : sobel_done_fence_;
        if (vkQueueSubmit2(queues_.get(QueueType::CMP), 1, &submit, fence) != VK_SUCCESS)
            throw std::runtime_error("async sobel: compute submit failed");
        if (fence != VK_NULL_HANDLE)
        {
            wait_fence();
            sobel_fence_in_flight_ = false;
        }
    };

    auto record_overlay_cb = [&](const SobelFrameRequest::Layer& layer, bool present_ready) {
        vkResetCommandBuffer(overlay_cb, 0);
        VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(overlay_cb, &bi);
        sobel_.record_edge_acquire_for_graphics(overlay_cb);

        VkRenderingAttachmentInfo color{};
        color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        color.imageView = swapchain_targets_.color_view(image_index);
        color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

        VkRenderingInfo ri{ VK_STRUCTURE_TYPE_RENDERING_INFO };
        ri.renderArea = { { 0, 0 }, { width, height } };
        ri.layerCount = 1;
        ri.colorAttachmentCount = 1;
        ri.pColorAttachments = &color;
        vkCmdBeginRendering(overlay_cb, &ri);
        sobel_.record_overlay(overlay_cb, width, height, highlight_for(layer.mode));
        vkCmdEndRendering(overlay_cb);

        if (present_ready)
            frame_recorder_.transition_color_to_present(overlay_cb, swapchainImages[image_index]);
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
            wait.semaphore = image_available;
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
            if (vkQueueSubmit2(queues_.get(QueueType::_3D), 1, &submit, VK_NULL_HANDLE) != VK_SUCCESS)
                throw std::runtime_error("async sobel: graphics submit failed");

            record_and_submit_compute(/*wait_g2c=*/true, /*signal_cfin=*/true);
        }
        else
        {
            // Fence-only extra layers â€” never re-signal g2c/cfin.
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
            if (vkQueueSubmit2(queues_.get(QueueType::_3D), 1, &submit, sobel_done_fence_) !=
                VK_SUCCESS)
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
                const uint64_t signal_value = frame_presenter_.frame_counter() + 1;
                VkSemaphoreSubmitInfo sigs[2]{};
                sigs[0].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
                sigs[0].semaphore = frame_sync_.timeline();
                sigs[0].value = signal_value;
                sigs[0].stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                sigs[1].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
                sigs[1].semaphore = render_finished;
                sigs[1].stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                VkSubmitInfo2 submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
                submit.commandBufferInfoCount = 1;
                submit.pCommandBufferInfos = &ocb;
                submit.waitSemaphoreInfoCount = 1;
                submit.pWaitSemaphoreInfos = waits;
                submit.signalSemaphoreInfoCount = 2;
                submit.pSignalSemaphoreInfos = sigs;
                if (vkQueueSubmit2(queues_.get(QueueType::_3D), 1, &submit, sobel_done_fence_) !=
                    VK_SUCCESS)
                    throw std::runtime_error("async sobel: overlay submit failed");
                sobel_fence_in_flight_ = true;

                VkPresentInfoKHR present{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
                present.waitSemaphoreCount = 1;
                present.pWaitSemaphores = &render_finished;
                present.swapchainCount = 1;
                present.pSwapchains = &swapchain;
                present.pImageIndices = &image_index;
                vkQueuePresentKHR(queues_.get(QueueType::_3D), &present);

                frame_sync_.set_frame_timeline_value(frame_index, signal_value);
                frame_presenter_.notify_submitted_and_presented();
            }
            else
            {
                // Finish layer 0 (retire cfin) before fence-only extra layers.
                VkSubmitInfo2 submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
                submit.commandBufferInfoCount = 1;
                submit.pCommandBufferInfos = &ocb;
                submit.waitSemaphoreInfoCount = 1;
                submit.pWaitSemaphoreInfos = waits;
                if (vkQueueSubmit2(queues_.get(QueueType::_3D), 1, &submit, sobel_done_fence_) !=
                    VK_SUCCESS)
                    throw std::runtime_error("async sobel: intermediate overlay submit failed");
                wait_fence();
            }
        }
        else if (last)
        {
            const uint64_t signal_value = frame_presenter_.frame_counter() + 1;
            VkSemaphoreSubmitInfo sigs[2]{};
            sigs[0].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            sigs[0].semaphore = frame_sync_.timeline();
            sigs[0].value = signal_value;
            sigs[0].stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            sigs[1].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            sigs[1].semaphore = render_finished;
            sigs[1].stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            VkSubmitInfo2 submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
            submit.commandBufferInfoCount = 1;
            submit.pCommandBufferInfos = &ocb;
            submit.signalSemaphoreInfoCount = 2;
            submit.pSignalSemaphoreInfos = sigs;
            if (vkQueueSubmit2(queues_.get(QueueType::_3D), 1, &submit, sobel_done_fence_) !=
                VK_SUCCESS)
                throw std::runtime_error("async sobel: final multi-layer overlay failed");
            sobel_fence_in_flight_ = true;

            VkPresentInfoKHR present{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
            present.waitSemaphoreCount = 1;
            present.pWaitSemaphores = &render_finished;
            present.swapchainCount = 1;
            present.pSwapchains = &swapchain;
            present.pImageIndices = &image_index;
            vkQueuePresentKHR(queues_.get(QueueType::_3D), &present);

            frame_sync_.set_frame_timeline_value(frame_index, signal_value);
            frame_presenter_.notify_submitted_and_presented();
        }
        else
        {
            VkSubmitInfo2 submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
            submit.commandBufferInfoCount = 1;
            submit.pCommandBufferInfos = &ocb;
            if (vkQueueSubmit2(queues_.get(QueueType::_3D), 1, &submit, sobel_done_fence_) !=
                VK_SUCCESS)
                throw std::runtime_error("async sobel: mid multi-layer overlay failed");
            wait_fence();
        }
    }
}


