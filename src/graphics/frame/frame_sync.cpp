#include "graphics/frame/frame_sync.hpp"

#include <stdexcept>

void FrameSync::create(const FrameSyncCreateInfo& info)
{
    if (info.device == VK_NULL_HANDLE || info.frames_in_flight == 0 || info.swapchain_image_count == 0)
        throw std::runtime_error("FrameSync::create: invalid create info");

    destroy(info.device);

    frames_in_flight_ = info.frames_in_flight;
    swapchain_images_ = info.swapchain_image_count;
    image_available_.assign(frames_in_flight_, VK_NULL_HANDLE);
    render_finished_.assign(swapchain_images_, VK_NULL_HANDLE);
    frame_timeline_value_.assign(frames_in_flight_, 0);

    VkSemaphoreTypeCreateInfo semaphoreTypeCI{};
    semaphoreTypeCI.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    semaphoreTypeCI.semaphoreType = VK_SEMAPHORE_TYPE_BINARY;
    semaphoreTypeCI.initialValue = 0;

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semaphoreInfo.pNext = &semaphoreTypeCI;

    for (uint32_t i = 0; i < frames_in_flight_; ++i)
    {
        if (vkCreateSemaphore(info.device, &semaphoreInfo, nullptr, &image_available_[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create image-available semaphore");
    }
    for (uint32_t i = 0; i < swapchain_images_; ++i)
    {
        if (vkCreateSemaphore(info.device, &semaphoreInfo, nullptr, &render_finished_[i]) !=
            VK_SUCCESS)
            throw std::runtime_error("Failed to create render-finished semaphore");
    }

    semaphoreTypeCI.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    if (vkCreateSemaphore(info.device, &semaphoreInfo, nullptr, &timeline_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create timeline semaphore");
}

void FrameSync::destroy(VkDevice device)
{
    if (device == VK_NULL_HANDLE)
        return;

    for (VkSemaphore s : render_finished_)
    {
        if (s != VK_NULL_HANDLE)
            vkDestroySemaphore(device, s, nullptr);
    }
    render_finished_.clear();

    for (VkSemaphore s : image_available_)
    {
        if (s != VK_NULL_HANDLE)
            vkDestroySemaphore(device, s, nullptr);
    }
    image_available_.clear();

    if (timeline_ != VK_NULL_HANDLE)
    {
        vkDestroySemaphore(device, timeline_, nullptr);
        timeline_ = VK_NULL_HANDLE;
    }

    frame_timeline_value_.clear();
    frames_in_flight_ = 0;
    swapchain_images_ = 0;
}

void FrameSync::wait_frame(VkDevice device, uint32_t frame_index) const
{
    if (frame_index >= frames_in_flight_ || timeline_ == VK_NULL_HANDLE)
        return;

    const uint64_t value = frame_timeline_value_[frame_index];
    if (value == 0)
        return;

    VkSemaphoreWaitInfo waitInfo{};
    waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    waitInfo.semaphoreCount = 1;
    waitInfo.pSemaphores = &timeline_;
    waitInfo.pValues = &value;
    vkWaitSemaphores(device, &waitInfo, UINT64_MAX);
}

VkSemaphore FrameSync::image_available(uint32_t frame_index) const
{
    return image_available_.at(frame_index);
}

VkSemaphore FrameSync::render_finished(uint32_t image_index) const
{
    return render_finished_.at(image_index);
}

void FrameSync::set_frame_timeline_value(uint32_t frame_index, uint64_t value)
{
    frame_timeline_value_.at(frame_index) = value;
}

uint64_t FrameSync::frame_timeline_value(uint32_t frame_index) const
{
    return frame_timeline_value_.at(frame_index);
}
