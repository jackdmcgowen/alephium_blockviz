#include "graphics/pch.h"
#include "graphics/frame/swapchain_targets.hpp"

#include "graphics/gpu_prv_lib.h"

#include <cstdio>
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

VkSampleCountFlagBits SwapchainTargets::pick_sample_count(VkPhysicalDevice pd,
                                                          VkSampleCountFlagBits requested)
{
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(pd, &props);
    const VkSampleCountFlags counts =
        props.limits.framebufferColorSampleCounts & props.limits.framebufferDepthSampleCounts;

    const VkSampleCountFlagBits try_order[] = {
        VK_SAMPLE_COUNT_4_BIT,
        VK_SAMPLE_COUNT_2_BIT,
        VK_SAMPLE_COUNT_1_BIT,
    };
    // Prefer requested if supported, else highest â‰¤ requested.
    if ((counts & requested) == requested)
        return requested;
    for (VkSampleCountFlagBits s : try_order)
    {
        if (s > requested)
            continue;
        if ((counts & s) == s)
            return s;
    }
    return VK_SAMPLE_COUNT_1_BIT;
}

bool SwapchainTargets::device_alpha_to_coverage(VkPhysicalDevice /*pd*/)
{
    // alphaToCoverageEnable is a pipeline multisample state; no separate feature bit.
    return true;
}

void SwapchainTargets::create_color_views(VkDevice device, const VkImage* images, uint32_t count,
                                          VkFormat color_format)
{
    color_views_.resize(count);
    for (uint32_t i = 0; i < count; ++i)
        color_views_[i] = create_image_view(device, images[i], color_format, VK_IMAGE_ASPECT_COLOR_BIT);
}

void SwapchainTargets::create_depth(VkDevice device, VkPhysicalDeviceMemoryProperties* mem_props,
                                    uint32_t width, uint32_t height, VkSampleCountFlagBits samples)
{
    if (depth_format_ == VK_FORMAT_UNDEFINED)
        depth_format_ = find_depth_format(physical_device_);

    // MSAA depth is attachment-only; 1Ã— depth also SAMPLED for legacy paths.
    VkImageUsageFlags usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (samples == VK_SAMPLE_COUNT_1_BIT)
        usage |= VK_IMAGE_USAGE_SAMPLED_BIT;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = { width, height, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = depth_format_;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = samples;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(device, &imageInfo, nullptr, &depth_image_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create depth image");

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, depth_image_, &memRequirements);
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = find_device_memory_type(
        mem_props, memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(device, &allocInfo, nullptr, &depth_memory_) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate depth memory");
    vkBindImageMemory(device, depth_image_, depth_memory_, 0);

    depth_view_ = create_image_view(device, depth_image_, depth_format_, VK_IMAGE_ASPECT_DEPTH_BIT);
}

void SwapchainTargets::create_msaa_color(VkDevice device, VkPhysicalDeviceMemoryProperties* mem_props,
                                         uint32_t width, uint32_t height, VkFormat color_format,
                                         VkSampleCountFlagBits samples)
{
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = { width, height, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = color_format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
    imageInfo.samples = samples;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(device, &imageInfo, nullptr, &msaa_color_image_) != VK_SUCCESS)
    {
        // Transient may fail on some drivers â€” retry without TRANSIENT
        imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        if (vkCreateImage(device, &imageInfo, nullptr, &msaa_color_image_) != VK_SUCCESS)
            throw std::runtime_error("Failed to create MSAA color image");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, msaa_color_image_, &memRequirements);
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = find_device_memory_type(
        mem_props, memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(device, &allocInfo, nullptr, &msaa_color_memory_) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate MSAA color memory");
    vkBindImageMemory(device, msaa_color_image_, msaa_color_memory_, 0);

    msaa_color_view_ = create_image_view(device, msaa_color_image_, color_format, VK_IMAGE_ASPECT_COLOR_BIT);
}

void SwapchainTargets::create(const SwapchainTargetsCreateInfo& info)
{
    if (!info.device || !info.physical_device || !info.mem_props ||
        !info.swapchain_images || info.swapchain_image_count == 0)
    {
        throw std::runtime_error("SwapchainTargets::create: invalid args");
    }

    physical_device_ = info.physical_device;
    sample_count_ = pick_sample_count(physical_device_, info.requested_samples);
    alpha_to_coverage_ = info.prefer_alpha_to_coverage &&
                         sample_count_ > VK_SAMPLE_COUNT_1_BIT &&
                         device_alpha_to_coverage(physical_device_);

    std::printf("[engine] MSAA samples=%u  alphaToCoverage=%s\n",
                static_cast<unsigned>(sample_count_),
                alpha_to_coverage_ ? "yes" : "no");

    create_color_views(info.device, info.swapchain_images, info.swapchain_image_count,
                       info.color_format);
    create_depth(info.device, info.mem_props, info.width, info.height, sample_count_);
    if (sample_count_ > VK_SAMPLE_COUNT_1_BIT)
        create_msaa_color(info.device, info.mem_props, info.width, info.height,
                          info.color_format, sample_count_);
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

    if (msaa_color_view_ != VK_NULL_HANDLE)
    {
        destroy_image_view(device, msaa_color_view_);
        msaa_color_view_ = VK_NULL_HANDLE;
    }
    if (msaa_color_image_ != VK_NULL_HANDLE)
    {
        destroy_image(device, msaa_color_image_, msaa_color_memory_);
        msaa_color_image_ = VK_NULL_HANDLE;
        msaa_color_memory_ = VK_NULL_HANDLE;
    }

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
