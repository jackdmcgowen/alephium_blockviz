#include "engine/swapchain_targets.hpp"

#include "graphics/gpu_prv_lib.h"

#include <stdexcept>

VkFormat SwapchainTargets::find_depth_format(VkPhysicalDevice physical_device)
{
    const VkFormat candidates[] = {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT,
    };

    for (VkFormat format : candidates)
    {
        VkFormatProperties props{};
        vkGetPhysicalDeviceFormatProperties(physical_device, format, &props);
        if ((props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) ==
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
        {
            return format;
        }
    }
    throw std::runtime_error("Failed to find supported depth format");
}

void SwapchainTargets::create_color_views(VkDevice device, const VkImage* images, uint32_t count,
                                          VkFormat color_format)
{
    color_views_.resize(count);
    for (uint32_t i = 0; i < count; ++i)
        color_views_[i] = create_image_view(device, images[i], color_format, VK_IMAGE_ASPECT_COLOR_BIT);
}

void SwapchainTargets::create_depth(VkDevice device, VkPhysicalDeviceMemoryProperties* mem_props,
                                    uint32_t width, uint32_t height)
{
    if (depth_format_ == VK_FORMAT_UNDEFINED)
        depth_format_ = find_depth_format(physical_device_);

    // SAMPLED: async depth-Sobel on CMP reads depth as a texture.
    create_image(
        device,
        width, height,
        depth_format_,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        depth_image_,
        depth_memory_,
        mem_props);
    depth_view_ = create_image_view(device, depth_image_, depth_format_, VK_IMAGE_ASPECT_DEPTH_BIT);
}

void SwapchainTargets::create(const SwapchainTargetsCreateInfo& info)
{
    if (!info.device || !info.physical_device || !info.mem_props ||
        !info.swapchain_images || info.swapchain_image_count == 0)
    {
        throw std::runtime_error("SwapchainTargets::create: invalid args");
    }

    physical_device_ = info.physical_device;
    create_color_views(info.device, info.swapchain_images, info.swapchain_image_count,
                       info.color_format);
    create_depth(info.device, info.mem_props, info.width, info.height);
}

void SwapchainTargets::destroy(VkDevice device)
{
    if (!device)
        return;

    for (VkImageView view : color_views_)
    {
        if (view != VK_NULL_HANDLE)
            destroy_image_view(device, view);
    }
    color_views_.clear();

    if (depth_view_ != VK_NULL_HANDLE)
    {
        destroy_image_view(device, depth_view_);
        depth_view_ = VK_NULL_HANDLE;
    }
    if (depth_image_ != VK_NULL_HANDLE)
    {
        destroy_image(device, depth_image_, depth_memory_);
        depth_image_ = VK_NULL_HANDLE;
        depth_memory_ = VK_NULL_HANDLE;
    }
    // Keep depth_format_ and physical_device_ so recreate can reuse format pick.
}

void SwapchainTargets::recreate(const SwapchainTargetsCreateInfo& info)
{
    destroy(info.device);
    create(info);
}

VkImageView SwapchainTargets::color_view(uint32_t index) const
{
    if (index >= color_views_.size())
        return VK_NULL_HANDLE;
    return color_views_[index];
}
