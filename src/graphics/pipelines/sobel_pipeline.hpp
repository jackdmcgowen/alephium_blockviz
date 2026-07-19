#pragma once

// Sobel pipeline assets: outline depth+color draw + compute edge detect + overlay.
// Images/descriptors live here; multi-queue submit is SobelAsyncPass (frame/).
// Scene depth is not used — only requested instances are redrawn into sel_depth_.
// Outline colors come from a compact instance buffer (app-fed); Sobel edge is white × color.
#include "graphics/queue_types.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>

struct SobelPipelineCreateInfo
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

// One clear + one multi-instance DrawIndexed of outline cubes (pos/scale/color).
// Cubes only — never debug arrows. Safety cap is high (see record_outline_pass).
struct OutlinePassDrawParams
{
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkDescriptorSet ubo_set = VK_NULL_HANDLE;
    VkBuffer vertex_buffer = VK_NULL_HANDLE;
    VkBuffer outline_instance_buffer = VK_NULL_HANDLE; // InstanceData[] compact
    VkBuffer index_buffer = VK_NULL_HANDLE;
    uint32_t outline_count = 0; // ≥1 when recording; capped at kMaxSobelInstances
    uint32_t index_count = 36;
    uint32_t width = 0;
    uint32_t height = 0;
};

// Soft safety ceiling (not a product limit of 32). All eligible cubes may be highlighted.
static constexpr uint32_t kMaxSobelInstances = 4096;

class SobelPipeline
{
public:
    void create(const SobelPipelineCreateInfo& info);
    void destroy(VkDevice device);
    void recreate(const SobelPipelineCreateInfo& info);

    // Clear depth+color, redraw N outline instances in one BeginRendering.
    // Leaves sel_depth_ as DEPTH_ATTACHMENT and outline_color as COLOR_ATTACHMENT.
    void record_outline_pass(const OutlinePassDrawParams& p);

    // Transition sel_depth_ DEPTH_ATTACHMENT → release for compute.
    void record_sel_depth_release_for_compute(VkCommandBuffer cmd);

    // COLOR_ATTACHMENT → SHADER_READ for overlay sampling (graphics family).
    void record_outline_color_to_shader_read(VkCommandBuffer cmd);

    // Compute: sample sel_depth_, write edge image (CMP-queue-safe stages only).
    void record_dispatch(VkCommandBuffer cmd, float strength, float threshold);

    // Graphics: acquire edge image + GENERAL → SHADER_READ before overlay sample.
    void record_edge_acquire_for_graphics(VkCommandBuffer cmd);

    // Fullscreen edge×color overlay into current color attachment (premultiplied).
    void record_overlay(VkCommandBuffer cmd, uint32_t width, uint32_t height,
                        float intensity = 1.0f);

    VkImage sel_depth_image() const { return sel_depth_image_; }
    VkImageView sel_depth_view() const { return sel_depth_view_; }

    // Per-frame binary semaphores (must match engine frames-in-flight).
    static constexpr uint32_t kMaxFrames = 3;
    VkSemaphore graphics_to_compute(uint32_t frame_index) const;
    VkSemaphore compute_finished(uint32_t frame_index) const;

    bool ready() const { return pipeline_ != VK_NULL_HANDLE && outline_pipeline_ != VK_NULL_HANDLE; }

private:
    void create_images(const SobelPipelineCreateInfo& info);
    void create_descriptors(VkDevice device);
    void write_static_descriptors_();
    void create_compute_pipeline(VkDevice device);
    void create_outline_pipeline(VkDevice device, VkFormat depth_format,
                                 VkDescriptorSetLayout ubo_layout);
    void create_overlay_pipeline(VkDevice device, VkFormat color_format);
    void create_sync(VkDevice device);

    VkDevice device_ = VK_NULL_HANDLE;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint32_t graphics_family_ = 0;
    uint32_t compute_family_ = 0;
    VkFormat depth_format_ = VK_FORMAT_UNDEFINED;

    // Outline-only depth (not the scene depth)
    VkImage sel_depth_image_ = VK_NULL_HANDLE;
    VkDeviceMemory sel_depth_memory_ = VK_NULL_HANDLE;
    VkImageView sel_depth_view_ = VK_NULL_HANDLE;

    // Per-pixel outline tint (written with depth; sampled by overlay)
    VkImage outline_color_image_ = VK_NULL_HANDLE;
    VkDeviceMemory outline_color_memory_ = VK_NULL_HANDLE;
    VkImageView outline_color_view_ = VK_NULL_HANDLE;

    VkImage edge_image_ = VK_NULL_HANDLE;
    VkDeviceMemory edge_memory_ = VK_NULL_HANDLE;
    VkImageView edge_view_ = VK_NULL_HANDLE;

    VkSampler depth_sampler_ = VK_NULL_HANDLE;
    VkSampler edge_sampler_ = VK_NULL_HANDLE;
    VkSampler outline_color_sampler_ = VK_NULL_HANDLE;

    VkDescriptorSetLayout compute_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet compute_set_ = VK_NULL_HANDLE;
    VkPipelineLayout compute_layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;

    // Outline depth+color redraw of all requested instances (single pass)
    VkPipelineLayout outline_layout_ = VK_NULL_HANDLE;
    VkPipeline outline_pipeline_ = VK_NULL_HANDLE;

    VkDescriptorSetLayout overlay_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorSet overlay_set_ = VK_NULL_HANDLE;
    VkPipelineLayout overlay_layout_ = VK_NULL_HANDLE;
    VkPipeline overlay_pipeline_ = VK_NULL_HANDLE;

    VkSemaphore compute_finished_[kMaxFrames]{};
    VkSemaphore graphics_to_compute_[kMaxFrames]{};
};
