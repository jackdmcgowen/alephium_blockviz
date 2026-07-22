#include "graphics/pch.h"
#include "graphics/graphics_system.hpp"
#include "graphics/platform/gfx_platform.hpp"
#include "graphics/gpu_prv_lib.h"
#include "common/time_util.hpp"
#include "common/env_util.hpp"

#include <cstdio>
#include <ctime>
#include <string>

namespace
{
std::string make_default_path()
{
    gfx_platform_ensure_directory("docs");
    gfx_platform_ensure_directory("docs/images");

    const std::time_t t = std::time(nullptr);
    std::tm tm_local{};
    if (!blockviz::local_time(tm_local, t))
        tm_local = {};
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

bool GraphicsSystem::begin_screenshot_frame_()
{
    screenshot_path_this_frame_.clear();
    screenshot_copy_recorded_ = false;
    {
        std::lock_guard<std::mutex> lock(screenshot_mutex_);
        if (screenshot_pending_path_.empty())
            return false;
        screenshot_path_this_frame_ = std::move(screenshot_pending_path_);
        screenshot_pending_path_.clear();
    }
    if (width == 0 || height == 0 || device == VK_NULL_HANDLE)
    {
        screenshot_path_this_frame_.clear();
        return false;
    }
    try
    {
        ensure_screenshot_staging_();
    }
    catch (...)
    {
        std::printf("[gfx] screenshot: staging alloc failed\n");
        screenshot_path_this_frame_.clear();
        return false;
    }
    return true;
}

void GraphicsSystem::ensure_screenshot_staging_()
{
    const VkDeviceSize need =
        static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * 4u;
    if (screenshot_staging_ != VK_NULL_HANDLE && screenshot_staging_size_ >= need &&
        screenshot_extent_w_ == width && screenshot_extent_h_ == height)
        return;

    destroy_screenshot_staging_();
    create_buffer(device, &deviceMemProps, need, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  screenshot_staging_, screenshot_staging_mem_);
    screenshot_staging_size_ = need;
    screenshot_extent_w_ = width;
    screenshot_extent_h_ = height;
}

void GraphicsSystem::destroy_screenshot_staging_()
{
    if (device != VK_NULL_HANDLE && screenshot_staging_ != VK_NULL_HANDLE)
        destroy_buffer(device, screenshot_staging_, screenshot_staging_mem_);
    screenshot_staging_ = VK_NULL_HANDLE;
    screenshot_staging_mem_ = VK_NULL_HANDLE;
    screenshot_staging_size_ = 0;
    screenshot_extent_w_ = screenshot_extent_h_ = 0;
}

void GraphicsSystem::record_screenshot_before_present_(VkCommandBuffer cmd, VkImage swapchain_image)
{
    if (!cmd || swapchain_image == VK_NULL_HANDLE || screenshot_staging_ == VK_NULL_HANDLE ||
        screenshot_path_this_frame_.empty())
        return;

    // Still acquired: last draw left COLOR_ATTACHMENT. Copy then present layout.
    cmd_image_barrier(cmd, swapchain_image,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_COPY_BIT,
        { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = { screenshot_extent_w_, screenshot_extent_h_, 1 };
    vkCmdCopyImageToBuffer(cmd, swapchain_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           screenshot_staging_, 1, &region);

    cmd_image_barrier(cmd, swapchain_image,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_ACCESS_2_TRANSFER_READ_BIT, 0,
        VK_PIPELINE_STAGE_2_COPY_BIT, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
        { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });

    screenshot_copy_recorded_ = true;
}

void GraphicsSystem::finish_screenshot_after_submit_()
{
    if (!screenshot_copy_recorded_ || screenshot_path_this_frame_.empty())
    {
        screenshot_path_this_frame_.clear();
        screenshot_copy_recorded_ = false;
        return;
    }

    const std::string path = std::move(screenshot_path_this_frame_);
    screenshot_path_this_frame_.clear();
    screenshot_copy_recorded_ = false;

    // Copy ran in the same submitted CB(s) as the frame; wait graphics before map.
    if (queues_.get(QueueType::_3D) != VK_NULL_HANDLE)
        vkQueueWaitIdle(queues_.get(QueueType::_3D));

    ensure_parent_dirs(path);

    bool ok = false;
    void* mapped = nullptr;
    if (screenshot_staging_mem_ != VK_NULL_HANDLE &&
        vkMapMemory(device, screenshot_staging_mem_, 0, screenshot_staging_size_, 0, &mapped) ==
            VK_SUCCESS &&
        mapped)
    {
        ok = gfx_platform_write_png_rgba(
            path.c_str(), static_cast<int>(screenshot_extent_w_),
            static_cast<int>(screenshot_extent_h_),
            static_cast<const unsigned char*>(mapped));
        vkUnmapMemory(device, screenshot_staging_mem_);
    }

    if (ok)
    {
        std::printf("[gfx] screenshot (GPU readback) saved: %s (%ux%u)\n", path.c_str(),
                    screenshot_extent_w_, screenshot_extent_h_);
        return;
    }

    std::printf("[gfx] screenshot: GPU readback failed\n");
    const bool allow_blit = blockviz::env_flag("BLOCKVIZ_SCREENSHOT_WINDOW_BLIT", false);
    if (!allow_blit || gfx_platform_is_headless() || !hwnd)
    {
        std::printf("[gfx] screenshot failed: %s (set BLOCKVIZ_SCREENSHOT_WINDOW_BLIT=1 for "
                    "legacy window blit)\n",
                    path.c_str());
        return;
    }
    if (gfx_platform_save_window_png(hwnd, path.c_str()))
        std::printf("[gfx] screenshot (window blit) saved: %s\n", path.c_str());
    else
        std::printf("[gfx] screenshot failed: %s\n", path.c_str());
}
