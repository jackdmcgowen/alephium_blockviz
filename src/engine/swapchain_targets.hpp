#pragma once

// Swapchain color image views + depth attachment (E9).
// Swapchain KHR object stays owned by the engine / graphics helpers.
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
};

class SwapchainTargets
{
public:
    void create(const SwapchainTargetsCreateInfo& info);
    void destroy(VkDevice device); // safe if never created / already destroyed

    // Destroy existing targets then create (same device/format policy as create).
    void recreate(const SwapchainTargetsCreateInfo& info);

    VkImageView color_view(uint32_t index) const;
    uint32_t color_view_count() const { return static_cast<uint32_t>(color_views_.size()); }

    VkImage depth_image() const { return depth_image_; }
    VkImageView depth_view() const { return depth_view_; }
    VkFormat depth_format() const { return depth_format_; }

private:
    static VkFormat find_depth_format(VkPhysicalDevice physical_device);

    void create_color_views(VkDevice device, const VkImage* images, uint32_t count,
                            VkFormat color_format);
    void create_depth(VkDevice device, VkPhysicalDeviceMemoryProperties* mem_props,
                      uint32_t width, uint32_t height);

    std::vector<VkImageView> color_views_;
    VkImage depth_image_ = VK_NULL_HANDLE;
    VkDeviceMemory depth_memory_ = VK_NULL_HANDLE;
    VkImageView depth_view_ = VK_NULL_HANDLE;
    VkFormat depth_format_ = VK_FORMAT_UNDEFINED;
    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
};
