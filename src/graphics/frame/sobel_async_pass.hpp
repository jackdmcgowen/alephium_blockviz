#pragma once

// Multi-queue Sobel execution: _3D outline depth+color → CMP dispatch → _3D edge overlay (+ present).
// Owns the chain fence (VUID-09600). Does not own PSOs/images — uses IPass nodes + SobelResources.
// Single pass: all outline cubes are drawn together; colors come from the outline instance buffer.

#include "graphics/frame/passes/sobel_resources.hpp"
#include "graphics/frame/passes/outline_pass.hpp"
#include "graphics/frame/passes/sobel_compute_pass.hpp"
#include "graphics/frame/passes/edge_overlay_pass.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>

class FramePresenter;
class FrameSync;
class FrameProfiler;
namespace frame_graph { class MainScenePass; }

struct SobelAsyncSubmitContext
{
    VkDevice device = VK_NULL_HANDLE;
    VkQueue  graphics_queue = VK_NULL_HANDLE;
    VkQueue  compute_queue  = VK_NULL_HANDLE;
    uint32_t width  = 0;
    uint32_t height = 0;

    VkCommandBuffer main_graphics_cb  = VK_NULL_HANDLE; // scene + pick + outline depth/color
    VkCommandBuffer compute_cb        = VK_NULL_HANDLE;
    VkCommandBuffer overlay_cb        = VK_NULL_HANDLE;

    VkSemaphore image_available = VK_NULL_HANDLE;
    VkSemaphore render_finished = VK_NULL_HANDLE;

    VkDescriptorSet frame_ubo_set = VK_NULL_HANDLE;
    VkBuffer vertex_buffer            = VK_NULL_HANDLE;
    VkBuffer outline_instance_buffer  = VK_NULL_HANDLE;
    VkBuffer index_buffer             = VK_NULL_HANDLE;
    uint32_t outline_count = 0;
    uint32_t index_count   = 36;

    VkImageView     swapchain_color_view = VK_NULL_HANDLE;
    VkImage         swapchain_image      = VK_NULL_HANDLE;
    VkSwapchainKHR  swapchain            = VK_NULL_HANDLE;

    frame_graph::MainScenePass* main_pass = nullptr; // end CB + default present transition
    FrameSync*      frame_sync = nullptr;
    FramePresenter* presenter = nullptr;
    FrameProfiler*  profiler  = nullptr;

    // Optional: COLOR_ATTACHMENT → (optional readback) → PRESENT. If null, uses main_pass transition.
    void (*before_present)(void* user, VkCommandBuffer cmd, VkImage swapchain_image) = nullptr;
    void* before_present_user = nullptr;
};

class SobelAsyncPass
{
public:
    void create(VkDevice device);
    void destroy(VkDevice device);
    // Wait for in-flight chain before resize/free.
    void wait_idle(VkDevice device);

    // Single-pass: outline depth+color → CMP Sobel → edge×color overlay → present.
    void submit(frame_graph::OutlinePass& outline,
                frame_graph::SobelComputePass& compute,
                frame_graph::EdgeOverlayPass& overlay,
                frame_graph::SobelResources& resources,
                const SobelAsyncSubmitContext& ctx,
                uint32_t frame_index,
                uint32_t image_index);

private:
    VkFence done_fence_ = VK_NULL_HANDLE;
    bool    fence_in_flight_ = false;
};
