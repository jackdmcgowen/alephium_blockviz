#pragma once

// Selection / confirmed-tip depth pass + async Sobel on CMP + edge overlay on _3D.
// Scene depth is not used — only the requested instances are redrawn into sel_depth_.
#include "graphics/queue_types.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>

struct SobelComputeCreateInfo
{
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties* mem_props = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
    VkFormat depth_format = VK_FORMAT_UNDEFINED;
    VkDescriptorSetLayout frame_ubo_layout = VK_NULL_HANDLE; // same as cube UBO set
    uint32_t graphics_family = 0;
    uint32_t compute_family = 0;
};

// One clear + N indexed draws (N ≤ 32) in a single BeginRendering — no re-clear between draws.
struct SelectionDepthDrawParams
{
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkDescriptorSet ubo_set = VK_NULL_HANDLE;
    VkBuffer vertex_buffer = VK_NULL_HANDLE;
    VkBuffer instance_buffer = VK_NULL_HANDLE;
    VkBuffer index_buffer = VK_NULL_HANDLE;
    const uint32_t* instance_indices = nullptr;
    uint32_t instance_index_count = 0; // required ≥1 when recording for sobel; hard-capped at 32
    uint32_t index_count = 36;
    uint32_t width = 0;
    uint32_t height = 0;
};

class SobelCompute
{
public:
    void create(const SobelComputeCreateInfo& info);
    void destroy(VkDevice device);
    void recreate(const SobelComputeCreateInfo& info);

    // Clear + redraw N instances into sel_depth_ (depth-only). Leaves sel_depth_ as DEPTH_ATTACHMENT.
    // Single BeginRendering: one LOAD_OP_CLEAR then N DrawIndexed — no re-clear between draws.
    void record_selection_depth(const SelectionDepthDrawParams& p);

    // Transition sel_depth_ DEPTH_ATTACHMENT → SHADER_READ (and queue release if needed).
    void record_sel_depth_release_for_compute(VkCommandBuffer cmd);

    // Compute: sample sel_depth_, write edge image (CMP-queue-safe stages only).
    void record_dispatch(VkCommandBuffer cmd, float strength, float threshold);

    // Graphics: acquire edge image + GENERAL → SHADER_READ before overlay sample.
    void record_edge_acquire_for_graphics(VkCommandBuffer cmd);

    // Fullscreen edge overlay into current color attachment (additive blend).
    void record_overlay(VkCommandBuffer cmd, uint32_t width, uint32_t height,
                        const float highlight_rgba[4]);

    VkImage sel_depth_image() const { return sel_depth_image_; }
    VkImageView sel_depth_view() const { return sel_depth_view_; }

    // Per-frame binary semaphores (must match engine frames-in-flight).
    static constexpr uint32_t kMaxFrames = 3;
    VkSemaphore graphics_to_compute(uint32_t frame_index) const;
    VkSemaphore compute_finished(uint32_t frame_index) const;

    bool ready() const { return pipeline_ != VK_NULL_HANDLE && depth_only_pipeline_ != VK_NULL_HANDLE; }

private:
    void create_images(const SobelComputeCreateInfo& info);
    void create_descriptors(VkDevice device);
    void write_static_descriptors_();
    void create_compute_pipeline(VkDevice device);
    void create_depth_only_pipeline(VkDevice device, VkFormat depth_format,
                                    VkDescriptorSetLayout ubo_layout);
    void create_overlay_pipeline(VkDevice device, VkFormat color_format);
    void create_sync(VkDevice device);

    VkDevice device_ = VK_NULL_HANDLE;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint32_t graphics_family_ = 0;
    uint32_t compute_family_ = 0;
    VkFormat depth_format_ = VK_FORMAT_UNDEFINED;

    // Selection-only depth (not the scene depth)
    VkImage sel_depth_image_ = VK_NULL_HANDLE;
    VkDeviceMemory sel_depth_memory_ = VK_NULL_HANDLE;
    VkImageView sel_depth_view_ = VK_NULL_HANDLE;

    VkImage edge_image_ = VK_NULL_HANDLE;
    VkDeviceMemory edge_memory_ = VK_NULL_HANDLE;
    VkImageView edge_view_ = VK_NULL_HANDLE;

    VkSampler depth_sampler_ = VK_NULL_HANDLE;
    VkSampler edge_sampler_ = VK_NULL_HANDLE;

    VkDescriptorSetLayout compute_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet compute_set_ = VK_NULL_HANDLE;
    VkPipelineLayout compute_layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;

    // Depth-only redraw of selection / tip instances
    VkPipelineLayout depth_only_layout_ = VK_NULL_HANDLE;
    VkPipeline depth_only_pipeline_ = VK_NULL_HANDLE;

    VkDescriptorSetLayout overlay_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorSet overlay_set_ = VK_NULL_HANDLE;
    VkPipelineLayout overlay_layout_ = VK_NULL_HANDLE;
    VkPipeline overlay_pipeline_ = VK_NULL_HANDLE;

    VkSemaphore compute_finished_[kMaxFrames]{};
    VkSemaphore graphics_to_compute_[kMaxFrames]{};
};
