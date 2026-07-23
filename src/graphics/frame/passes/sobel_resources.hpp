#pragma once

// Shared Sobel images, descriptors, and cross-queue semaphores.
// Owned by GraphicsSystem; Outline / Compute / Overlay IPass attach and use.
// Multi-queue submit stays in SobelAsyncPass (executor).

#include "graphics/frame/frame_graph/ipass.hpp"
#include "graphics/core/sampler.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>

namespace frame_graph
{

// Soft safety ceiling (not a product limit). All eligible cubes may be highlighted.
static constexpr uint32_t kMaxSobelInstances = 4096;
static constexpr uint32_t kSobelMaxFrames = 3;

class SobelResources
{
public:
    void create(const PassCreateInfo& info);
    void destroy(VkDevice device);
    void recreate(const PassCreateInfo& info);

    bool ready() const
    {
        return sel_depth_image_ != VK_NULL_HANDLE && edge_image_ != VK_NULL_HANDLE &&
               compute_set_ != VK_NULL_HANDLE && overlay_set_ != VK_NULL_HANDLE;
    }

    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }
    uint32_t graphics_family() const { return graphics_family_; }
    uint32_t compute_family() const { return compute_family_; }

    VkImage sel_depth_image() const { return sel_depth_image_; }
    VkImageView sel_depth_view() const { return sel_depth_view_; }
    VkImage outline_color_image() const { return outline_color_image_; }
    VkImageView outline_color_view() const { return outline_color_view_; }
    VkImage edge_image() const { return edge_image_; }
    VkImageView edge_view() const { return edge_view_; }

    VkDescriptorSetLayout compute_set_layout() const { return compute_set_layout_; }
    VkDescriptorSetLayout overlay_set_layout() const { return overlay_set_layout_; }
    VkDescriptorSet compute_set() const { return compute_set_; }
    VkDescriptorSet overlay_set() const { return overlay_set_; }

    VkSemaphore graphics_to_compute(uint32_t frame_index) const;
    VkSemaphore compute_finished(uint32_t frame_index) const;

private:
    void create_images_(const PassCreateInfo& info);
    void create_descriptors_(VkDevice device);
    void write_static_descriptors_();
    void create_sync_(VkDevice device);

    VkDevice device_ = VK_NULL_HANDLE;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint32_t graphics_family_ = 0;
    uint32_t compute_family_ = 0;
    VkFormat depth_format_ = VK_FORMAT_UNDEFINED;
    const SamplerTable* samplers_ = nullptr;

    VkImage sel_depth_image_ = VK_NULL_HANDLE;
    VkDeviceMemory sel_depth_memory_ = VK_NULL_HANDLE;
    VkImageView sel_depth_view_ = VK_NULL_HANDLE;

    VkImage outline_color_image_ = VK_NULL_HANDLE;
    VkDeviceMemory outline_color_memory_ = VK_NULL_HANDLE;
    VkImageView outline_color_view_ = VK_NULL_HANDLE;

    VkImage edge_image_ = VK_NULL_HANDLE;
    VkDeviceMemory edge_memory_ = VK_NULL_HANDLE;
    VkImageView edge_view_ = VK_NULL_HANDLE;

    VkDescriptorSetLayout compute_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout overlay_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet compute_set_ = VK_NULL_HANDLE;
    VkDescriptorSet overlay_set_ = VK_NULL_HANDLE;

    VkSemaphore compute_finished_[kSobelMaxFrames]{};
    VkSemaphore graphics_to_compute_[kSobelMaxFrames]{};
};

} // namespace frame_graph
