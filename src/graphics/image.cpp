#include "graphics/pch.h"
#include "gpu_prv_lib.h"

void create_image(
    VkDevice device,
    uint32_t width,
    uint32_t height,
    VkFormat format,
    VkImageTiling tiling,
    VkImageUsageFlags usage,
    VkMemoryPropertyFlags properties,
    VkImage& image,
    VkDeviceMemory& imageMemory,
    VkPhysicalDeviceMemoryProperties *deviceMemProps)
{
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = tiling;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create depth image");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = find_device_memory_type(deviceMemProps, memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &imageMemory) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to allocate depth image memory");
    }
    vkBindImageMemory(device, image, imageMemory, 0);

}   /* create_image() */

VkImageView create_image_view(VkDevice device, VkImage image, VkFormat format, VkImageAspectFlags aspectFlags)
{
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView imageView;
    if (vkCreateImageView(device, &viewInfo, nullptr, &imageView) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create depth image view");
    }
    return imageView;

}	/* create_image_view() */


void destroy_image(VkDevice device, VkImage image, VkDeviceMemory imageMemory)
{
    vkDestroyImage(device, image, nullptr);

    vkFreeMemory(device, imageMemory, nullptr);

}   /* destroy_image() */


void destroy_image_view(VkDevice device, VkImageView imageview)
{
    vkDestroyImageView(device, imageview, nullptr);

}   /* destroy_image_view() */

void cmd_image_barrier(VkCommandBuffer cmd, VkImage image, VkImageLayout old_layout,
                       VkImageLayout new_layout, VkAccessFlags2 src_access,
                       VkAccessFlags2 dst_access, VkPipelineStageFlags2 src_stage,
                       VkPipelineStageFlags2 dst_stage, VkImageSubresourceRange range,
                       uint32_t src_queue_family, uint32_t dst_queue_family)
{
    if (cmd == VK_NULL_HANDLE || image == VK_NULL_HANDLE)
        return;

    VkImageMemoryBarrier2 barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
    barrier.srcStageMask = src_stage;
    barrier.srcAccessMask = src_access;
    barrier.dstStageMask = dst_stage;
    barrier.dstAccessMask = dst_access;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = src_queue_family;
    barrier.dstQueueFamilyIndex = dst_queue_family;
    barrier.image = image;
    barrier.subresourceRange = range;

    VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dep);
}

void cmd_image_barrier_aspect(VkCommandBuffer cmd, VkImage image, VkImageLayout old_layout,
                              VkImageLayout new_layout, VkAccessFlags2 src_access,
                              VkAccessFlags2 dst_access, VkPipelineStageFlags2 src_stage,
                              VkPipelineStageFlags2 dst_stage, VkImageAspectFlags aspect,
                              uint32_t src_queue_family, uint32_t dst_queue_family)
{
    const VkImageSubresourceRange range{ aspect, 0, 1, 0, 1 };
    cmd_image_barrier(cmd, image, old_layout, new_layout, src_access, dst_access, src_stage,
                      dst_stage, range, src_queue_family, dst_queue_family);
}
