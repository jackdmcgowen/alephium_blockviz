#include "gpu_prv_lib.h"

void create_swapchain(
    VkDevice device,
    VkSurfaceKHR surface,
    VkSwapchainKHR *swapchain,
    std::vector<VkImage> &swapchainImages,
    VkFormat format,
    VkExtent2D extent)
{
    const VkSwapchainKHR old_swapchain = (swapchain != nullptr) ? *swapchain : VK_NULL_HANDLE;

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;
    createInfo.minImageCount = MAX_SWAPCHAIN_IMAGES;
    createInfo.oldSwapchain = old_swapchain;
    createInfo.imageFormat = format;
    createInfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    createInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    createInfo.clipped = VK_TRUE;
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkSwapchainKHR new_swapchain = VK_NULL_HANDLE;
    if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &new_swapchain) != VK_SUCCESS)
        throw std::runtime_error("Failed to create swapchain");

    // Retire old swapchain after successful create (was leaked every resize).
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
