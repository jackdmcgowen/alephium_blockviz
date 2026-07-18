#include "graphics/pch.h"
#include "gpu_prv_lib.h"

#include <algorithm>

void create_swapchain(
    VkDevice device,
    VkPhysicalDevice physical_device,
    VkSurfaceKHR surface,
    VkSwapchainKHR *swapchain,
    std::vector<VkImage> &swapchainImages,
    VkFormat format,
    VkExtent2D *extent)
{
    if (!extent)
        throw std::runtime_error("create_swapchain: null extent");

    const VkSwapchainKHR old_swapchain = (swapchain != nullptr) ? *swapchain : VK_NULL_HANDLE;

    VkSurfaceCapabilitiesKHR caps{};
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &caps) != VK_SUCCESS)
        throw std::runtime_error("Failed to query surface capabilities for swapchain");

    // Clamp extent (0 = minimized / special currentExtent on some drivers).
    VkExtent2D use_extent = *extent;
    if (caps.currentExtent.width != UINT32_MAX)
    {
        use_extent = caps.currentExtent;
    }
    else
    {
        use_extent.width = std::clamp(use_extent.width, caps.minImageExtent.width,
                                      caps.maxImageExtent.width);
        use_extent.height = std::clamp(use_extent.height, caps.minImageExtent.height,
                                       caps.maxImageExtent.height);
    }
    if (use_extent.width == 0 || use_extent.height == 0)
    {
        use_extent.width = std::max(1u, caps.minImageExtent.width);
        use_extent.height = std::max(1u, caps.minImageExtent.height);
        use_extent.width = std::min(use_extent.width, caps.maxImageExtent.width);
        use_extent.height = std::min(use_extent.height, caps.maxImageExtent.height);
    }
    *extent = use_extent;

    VkSurfaceTransformFlagBitsKHR pre_transform = caps.currentTransform;
    if (caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
        pre_transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;

    uint32_t min_images = MAX_SWAPCHAIN_IMAGES;
    if (caps.maxImageCount > 0 && min_images > caps.maxImageCount)
        min_images = caps.maxImageCount;
    if (min_images < caps.minImageCount)
        min_images = caps.minImageCount;

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;
    createInfo.minImageCount = min_images;
    createInfo.oldSwapchain = old_swapchain;
    createInfo.imageFormat = format;
    createInfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    createInfo.imageExtent = use_extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    createInfo.preTransform = pre_transform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    createInfo.clipped = VK_TRUE;
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkSwapchainKHR new_swapchain = VK_NULL_HANDLE;
    if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &new_swapchain) != VK_SUCCESS)
        throw std::runtime_error("Failed to create swapchain");

    if (old_swapchain != VK_NULL_HANDLE)
        vkDestroySwapchainKHR(device, old_swapchain, nullptr);

    *swapchain = new_swapchain;

    uint32_t imageCount = 0;
    vkGetSwapchainImagesKHR(device, *swapchain, &imageCount, nullptr);
    swapchainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(device, *swapchain, &imageCount, swapchainImages.data());

}   /* create_swapchain() */


void destroy_swapchain(VkDevice device, VkSwapchainKHR swapchain)
{
    if (swapchain != VK_NULL_HANDLE)
        vkDestroySwapchainKHR(device, swapchain, nullptr);

}   /* destroy_swapchain() */
