#pragma once

// UBO descriptor set layout + shared pool (includes ImGui sampler budget) + set (E11).
#include <vulkan/vulkan.h>

#include <cstdint>

struct FrameDescriptorsCreateInfo
{
    VkDevice device = VK_NULL_HANDLE;
    VkBuffer ubo_buffer = VK_NULL_HANDLE;
    VkDeviceSize ubo_range = 0;
    // Extra pool room for ImGui (combined image samplers). Host/engine passes
    // IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE (or larger).
    uint32_t combined_image_sampler_count = 1;
    uint32_t max_sets = 2;
};

class FrameDescriptors
{
public:
    void create(const FrameDescriptorsCreateInfo& info);
    void destroy(VkDevice device); // safe if never created

    VkDescriptorSetLayout layout() const { return layout_; }
    VkDescriptorPool pool() const { return pool_; }
    VkDescriptorSet set() const { return set_; }

private:
    VkDescriptorSetLayout layout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkDescriptorSet set_ = VK_NULL_HANDLE;
};
