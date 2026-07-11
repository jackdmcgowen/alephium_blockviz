#pragma once

// Swapchain color views + scene depth + optional 4× MSAA targets (E9 / G4).
#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

struct SwapchainTargetsCreateInfo
{
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties* mem_props = nullptr;
    const VkImage* swapchain_images = nullptr;
    uint32_t swapchain_image_count = 0;
    VkFormat color_format = VK_FORMAT_UNDEFINED;
    uint32_t width = 0;
    uint32_t height = 0;
    // Requested samples (1 or 4). Clamped to device limits.
    VkSampleCountFlagBits requested_samples = VK_SAMPLE_COUNT_4_BIT;
    bool prefer_alpha_to_coverage = true;
};

class SwapchainTargets
{
public:
    void create(const SwapchainTargetsCreateInfo& info);
    void destroy(VkDevice device);
    void recreate(const SwapchainTargetsCreateInfo& info);

    VkImageView color_view(uint32_t index) const;
    uint32_t color_view_count() const { return static_cast<uint32_t>(color_views_.size()); }

    VkImage depth_image() const { return depth_image_; }
    VkImageView depth_view() const { return depth_view_; }
    VkFormat depth_format() const { return depth_format_; }

    // MSAA (sample_count_ > 1)
    VkSampleCountFlagBits sample_count() const { return sample_count_; }
    bool msaa_enabled() const { return sample_count_ > VK_SAMPLE_COUNT_1_BIT; }
    bool alpha_to_coverage() const { return alpha_to_coverage_; }
    VkImage msaa_color_image() const { return msaa_color_image_; }
    VkImageView msaa_color_view() const { return msaa_color_view_; }
    // MSAA depth reuses depth_image()/depth_view() when sample_count() > 1.

    // Resolve target for main pass = swapchain view at index
    VkImageView resolve_color_view(uint32_t index) const { return color_view(index); }

private:
    static VkFormat find_depth_format(VkPhysicalDevice physical_device);
    static VkSampleCountFlagBits pick_sample_count(VkPhysicalDevice pd,
                                                   VkSampleCountFlagBits requested);
    static bool device_alpha_to_coverage(VkPhysicalDevice pd);

    void create_color_views(VkDevice device, const VkImage* images, uint32_t count,
                            VkFormat color_format);
    void create_depth(VkDevice device, VkPhysicalDeviceMemoryProperties* mem_props,
                      uint32_t width, uint32_t height, VkSampleCountFlagBits samples);
    void create_msaa_color(VkDevice device, VkPhysicalDeviceMemoryProperties* mem_props,
                           uint32_t width, uint32_t height, VkFormat color_format,
                           VkSampleCountFlagBits samples);

    std::vector<VkImageView> color_views_;
    VkImage depth_image_ = VK_NULL_HANDLE;
    VkDeviceMemory depth_memory_ = VK_NULL_HANDLE;
    VkImageView depth_view_ = VK_NULL_HANDLE;
    VkFormat depth_format_ = VK_FORMAT_UNDEFINED;
    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;

    VkSampleCountFlagBits sample_count_ = VK_SAMPLE_COUNT_1_BIT;
    bool alpha_to_coverage_ = false;

    VkImage msaa_color_image_ = VK_NULL_HANDLE;
    VkDeviceMemory msaa_color_memory_ = VK_NULL_HANDLE;
    VkImageView msaa_color_view_ = VK_NULL_HANDLE;
    // When MSAA: depth is also multi-sampled (depth_image_/view_)
};
