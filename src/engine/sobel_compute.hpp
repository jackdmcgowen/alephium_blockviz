#pragma once

// Async depth-Sobel on CMP queue + edge overlay composite on _3D (selection highlight).
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
    uint32_t graphics_family = 0;
    uint32_t compute_family = 0;
};

class SobelCompute
{
public:
    void create(const SobelComputeCreateInfo& info);
    void destroy(VkDevice device);
    void recreate(const SobelComputeCreateInfo& info);

    // Record compute dispatch: sample depthView, write edge image.
    // depth must be in SHADER_READ_ONLY_OPTIMAL for graphics family (caller barriers).
    void record_dispatch(VkCommandBuffer cmd, VkImageView depth_view,
                         float strength, float threshold);

    // Fullscreen edge overlay into current color attachment (additive blend).
    void record_overlay(VkCommandBuffer cmd, uint32_t width, uint32_t height,
                        const float highlight_rgba[4]);

    VkImage edge_image() const { return edge_image_; }
    VkImageView edge_view() const { return edge_view_; }
    VkSemaphore compute_finished() const { return compute_finished_; }

    bool ready() const { return pipeline_ != VK_NULL_HANDLE; }

private:
    void create_edge_image(const SobelComputeCreateInfo& info);
    void create_descriptors(VkDevice device);
    void create_compute_pipeline(VkDevice device);
    void create_overlay_pipeline(VkDevice device, VkFormat color_format, VkFormat depth_format);
    void create_sync(VkDevice device);

    VkDevice device_ = VK_NULL_HANDLE;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint32_t graphics_family_ = 0;
    uint32_t compute_family_ = 0;

    VkImage edge_image_ = VK_NULL_HANDLE;
    VkDeviceMemory edge_memory_ = VK_NULL_HANDLE;
    VkImageView edge_view_ = VK_NULL_HANDLE;

    VkSampler depth_sampler_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout compute_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet compute_set_ = VK_NULL_HANDLE;
    VkPipelineLayout compute_layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;

    VkDescriptorSetLayout overlay_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorSet overlay_set_ = VK_NULL_HANDLE;
    VkPipelineLayout overlay_layout_ = VK_NULL_HANDLE;
    VkPipeline overlay_pipeline_ = VK_NULL_HANDLE;
    VkSampler edge_sampler_ = VK_NULL_HANDLE;

    VkSemaphore compute_finished_ = VK_NULL_HANDLE;
    VkSemaphore graphics_to_compute_ = VK_NULL_HANDLE;

public:
    VkSemaphore graphics_to_compute() const { return graphics_to_compute_; }
};
