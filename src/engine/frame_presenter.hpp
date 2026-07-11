#pragma once

// Acquire / submit / present + timeline bookkeeping (E13).
// Pick resolve and command recording stay on the engine.
#include "engine/frame_sync.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>

class FramePresenter
{
public:
    struct BeginResult
    {
        uint32_t frame_index = 0;
        // True when a prior acquire reported out-of-date/suboptimal.
        bool run_deferred_resize = false;
    };

    struct AcquireResult
    {
        uint32_t image_index = 0;
        VkSemaphore image_available = VK_NULL_HANDLE;
        VkSemaphore render_finished = VK_NULL_HANDLE;
    };

    // Wait for the in-flight slot; report whether resize should run now.
    BeginResult begin(VkDevice device, FrameSync& sync, uint32_t frames_in_flight);

    // Acquire next swapchain image; may set deferred resize on out-of-date/suboptimal.
    AcquireResult acquire(VkDevice device, VkSwapchainKHR swapchain, FrameSync& sync,
                          uint32_t frame_index);

    // vkQueueSubmit2 (timeline + render-finished) + present + advance frame counter.
    void submit_and_present(VkQueue queue,
                            VkSwapchainKHR swapchain,
                            FrameSync& sync,
                            uint32_t frame_index,
                            uint32_t image_index,
                            VkCommandBuffer command_buffer,
                            VkSemaphore image_available,
                            VkSemaphore render_finished);

    uint64_t frame_counter() const { return frame_counter_; }

    // After a custom multi-queue submit path that already signaled timeline value
    // (frame_counter_+1) and presented — keep counter in sync with submit_and_present.
    void notify_submitted_and_presented() { ++frame_counter_; }

private:
    uint64_t frame_counter_ = 0;
    bool pending_resize_ = false;
};
