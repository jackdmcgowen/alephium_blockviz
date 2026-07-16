#pragma once

#include <vulkan/vulkan.h>

struct CubePipeline
{
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;

    void create(VkDevice device,
                VkDescriptorSetLayout set_layout,
                VkFormat color_format,
                VkFormat depth_format,
                uint32_t viewport_width,
                uint32_t viewport_height,
                VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT,
                bool alpha_to_coverage = false);
    void destroy(VkDevice device);
};
