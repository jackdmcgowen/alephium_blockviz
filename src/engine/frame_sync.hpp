#pragma once

// Frame GPU synchronization (E3): binary acquire/present semaphores + timeline.
// Does not own command buffers or pick policy (those stay on the engine).
#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

struct FrameSyncCreateInfo
{
    VkDevice device = VK_NULL_HANDLE;
    uint32_t frames_in_flight = 3;
    uint32_t swapchain_image_count = 3;
};

class FrameSync
{
public:
    void create(const FrameSyncCreateInfo& info);
    void destroy(VkDevice device);

    void wait_frame(VkDevice device, uint32_t frame_index) const;

    VkSemaphore image_available(uint32_t frame_index) const;
    // Per frames-in-flight (not per swapchain image) — safer binary reuse with wait_frame.
    VkSemaphore render_finished(uint32_t frame_index) const;
    VkSemaphore timeline() const { return timeline_; }

    void set_frame_timeline_value(uint32_t frame_index, uint64_t value);
    uint64_t frame_timeline_value(uint32_t frame_index) const;

    uint32_t frames_in_flight() const { return frames_in_flight_; }
    uint32_t swapchain_image_count() const { return swapchain_images_; }

private:
    std::vector<VkSemaphore> image_available_;
    std::vector<VkSemaphore> render_finished_;
    std::vector<uint64_t>    frame_timeline_value_;
    VkSemaphore timeline_ = VK_NULL_HANDLE;
    uint32_t frames_in_flight_ = 0;
    uint32_t swapchain_images_ = 0;
};
