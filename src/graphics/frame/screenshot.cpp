#include "graphics/pch.h"
#include "graphics/graphics_system.hpp"
#include "graphics/platform/gfx_platform.hpp"
#include "graphics/gpu_prv_lib.h"

#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

namespace
{
std::string make_default_path()
{
    gfx_platform_ensure_directory("docs");
    gfx_platform_ensure_directory("docs/images");

    const std::time_t t = std::time(nullptr);
    std::tm tm_local{};
#if defined(_WIN32)
    localtime_s(&tm_local, &t);
#else
    localtime_r(&t, &tm_local);
#endif
    char name[256]{};
    std::snprintf(name, sizeof(name), "docs/images/capture_%04d%02d%02d_%02d%02d%02d.png",
                  tm_local.tm_year + 1900, tm_local.tm_mon + 1, tm_local.tm_mday,
                  tm_local.tm_hour, tm_local.tm_min, tm_local.tm_sec);
    return name;
}

void ensure_parent_dirs(const std::string& path)
{
    const auto slash = path.find_last_of("/\\");
    if (slash != std::string::npos)
    {
        const std::string dir = path.substr(0, slash);
        if (!dir.empty())
            gfx_platform_ensure_directory(dir.c_str());
    }
}
} // namespace

void GraphicsSystem::request_screenshot(const char* path_utf8)
{
    std::lock_guard<std::mutex> lock(screenshot_mutex_);
    if (path_utf8 && path_utf8[0])
        screenshot_pending_path_ = path_utf8;
    else
        screenshot_pending_path_ = make_default_path();
}

bool GraphicsSystem::consume_and_save_screenshot_()
{
    std::string path;
    {
        std::lock_guard<std::mutex> lock(screenshot_mutex_);
        if (screenshot_pending_path_.empty())
            return false;
        path = std::move(screenshot_pending_path_);
        screenshot_pending_path_.clear();
    }

    ensure_parent_dirs(path);

    // --- Preferred: GPU readback of last presented swapchain image ---
    if (device != VK_NULL_HANDLE && last_swapchain_image_valid_ &&
        last_swapchain_image_index_ < swapchainImages.size() && width > 0 && height > 0)
    {
        const VkImage src = swapchainImages[last_swapchain_image_index_];
        const VkDeviceSize row_pitch = static_cast<VkDeviceSize>(width) * 4u;
        const VkDeviceSize buf_size = row_pitch * height;

        VkBuffer staging = VK_NULL_HANDLE;
        VkDeviceMemory staging_mem = VK_NULL_HANDLE;
        try
        {
            create_buffer(device, &deviceMemProps, buf_size,
                          VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          staging, staging_mem);
        }
        catch (...)
        {
            std::printf("[gfx] screenshot: staging buffer alloc failed\n");
            goto fallback_window;
        }

        // One-shot CB on graphics pool.
        VkCommandBufferAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool = commandPool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        VkCommandBuffer cb = VK_NULL_HANDLE;
        if (vkAllocateCommandBuffers(device, &ai, &cb) != VK_SUCCESS)
        {
            destroy_buffer(device, staging, staging_mem);
            goto fallback_window;
        }

        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cb, &bi);

        // PRESENT_SRC → TRANSFER_SRC
        VkImageMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.image = src;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;

        VkDependencyInfo dep{};
        dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(cb, &dep);

        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = { width, height, 1 };
        vkCmdCopyImageToBuffer(cb, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging, 1, &region);

        // TRANSFER_SRC → PRESENT_SRC
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
        barrier.dstAccessMask = 0;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        vkCmdPipelineBarrier2(cb, &dep);

        vkEndCommandBuffer(cb);

        VkCommandBufferSubmitInfo cbs{};
        cbs.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        cbs.commandBuffer = cb;
        VkSubmitInfo2 submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        submit.commandBufferInfoCount = 1;
        submit.pCommandBufferInfos = &cbs;

        const bool ok_submit =
            vkQueueSubmit2(queues_.get(QueueType::_3D), 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS;
        if (ok_submit)
            vkQueueWaitIdle(queues_.get(QueueType::_3D));
        vkFreeCommandBuffers(device, commandPool, 1, &cb);

        bool ok = false;
        if (ok_submit)
        {
            void* mapped = nullptr;
            if (vkMapMemory(device, staging_mem, 0, buf_size, 0, &mapped) == VK_SUCCESS && mapped)
            {
                ok = gfx_platform_write_png_rgba(
                    path.c_str(), static_cast<int>(width), static_cast<int>(height),
                    static_cast<const unsigned char*>(mapped));
                vkUnmapMemory(device, staging_mem);
            }
        }
        destroy_buffer(device, staging, staging_mem);

        if (ok)
        {
            std::printf("[gfx] screenshot (GPU readback) saved: %s (%ux%u)\n", path.c_str(),
                        width, height);
            return true;
        }
        std::printf("[gfx] screenshot: GPU readback failed, trying window blit\n");
    }

fallback_window:
    if (gfx_platform_is_headless() || !hwnd)
    {
        std::printf("[gfx] screenshot failed (no GPU capture, headless or no window): %s\n",
                    path.c_str());
        return false;
    }
    const bool ok = gfx_platform_save_window_png(hwnd, path.c_str());
    if (ok)
        std::printf("[gfx] screenshot saved: %s\n", path.c_str());
    else
        std::printf("[gfx] screenshot failed: %s\n", path.c_str());
    return ok;
}
