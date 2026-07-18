#include "graphics/frame/frame_presenter.hpp"

#include <stdexcept>

FramePresenter::BeginResult FramePresenter::begin(VkDevice device, FrameSync& sync,
                                                    uint32_t frames_in_flight)
{
    BeginResult out{};
    out.frame_index = static_cast<uint32_t>(frame_counter_ % frames_in_flight);
    sync.wait_frame(device, out.frame_index);

    if (pending_resize_)
    {
        out.run_deferred_resize = true;
        pending_resize_ = false;
    }
    return out;
}

FramePresenter::AcquireResult FramePresenter::acquire(VkDevice device, VkSwapchainKHR swapchain,
                                                        FrameSync& sync, uint32_t frame_index)
{
    AcquireResult out{};
    out.image_available = sync.image_available(frame_index);

    const VkResult result = vkAcquireNextImageKHR(
        device, swapchain, UINT64_MAX, out.image_available, VK_NULL_HANDLE, &out.image_index);

    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        pending_resize_ = true;
        out.ok = false; // do not submit/present this frame
        return out;
    }
    if (result == VK_SUBOPTIMAL_KHR)
        pending_resize_ = true;
    else if (result != VK_SUCCESS)
        throw std::runtime_error("Failed to acquire swapchain image");

    // Present wait/signal is per swapchain image (not frames-in-flight).
    out.render_finished = sync.render_finished(out.image_index);
    out.ok = true;
    return out;
}

void FramePresenter::submit_and_present(VkQueue queue,
                                        VkSwapchainKHR swapchain,
                                        FrameSync& sync,
                                        uint32_t frame_index,
                                        uint32_t image_index,
                                        VkCommandBuffer command_buffer,
                                        VkSemaphore image_available,
                                        VkSemaphore render_finished)
{
    const uint64_t signal_values[] = { frame_counter_ + 1, 0 };

    VkCommandBufferSubmitInfo cb_submit{};
    cb_submit.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cb_submit.commandBuffer = command_buffer;

    VkSemaphoreSubmitInfo wait_info{};
    wait_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    wait_info.semaphore = image_available;
    wait_info.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSemaphoreSubmitInfo signal_info[2]{};
    signal_info[0].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signal_info[0].semaphore = sync.timeline();
    signal_info[0].value = signal_values[0];
    signal_info[0].stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

    signal_info[1].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signal_info[1].semaphore = render_finished;
    signal_info[1].value = signal_values[1];
    signal_info[1].stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo2 submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cb_submit;
    submit.signalSemaphoreInfoCount = 2;
    submit.pSignalSemaphoreInfos = signal_info;
    submit.waitSemaphoreInfoCount = 1;
    submit.pWaitSemaphoreInfos = &wait_info;

    if (vkQueueSubmit2(queue, 1, &submit, VK_NULL_HANDLE) != VK_SUCCESS)
        throw std::runtime_error("Failed to submit draw command buffer");

    VkPresentInfoKHR present{};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &render_finished;
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain;
    present.pImageIndices = &image_index;

    vkQueuePresentKHR(queue, &present);

    sync.set_frame_timeline_value(frame_index, signal_values[0]);
    ++frame_counter_;
}
