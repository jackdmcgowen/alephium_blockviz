#include "graphics/pch.h"
#include "graphics/frame/frame_descriptors.hpp"

#include <stdexcept>

void FrameDescriptors::create(const FrameDescriptorsCreateInfo& info)
{
    if (!info.device || !info.ubo_buffer || info.ubo_range == 0)
        throw std::runtime_error("FrameDescriptors::create: invalid args");

    VkDescriptorSetLayoutBinding ubo_binding{};
    ubo_binding.binding = 0;
    ubo_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ubo_binding.descriptorCount = 1;
    ubo_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = 1;
    layout_info.pBindings = &ubo_binding;

    if (vkCreateDescriptorSetLayout(info.device, &layout_info, nullptr, &layout_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create descriptor set layout");

    const uint32_t sampler_count =
        info.combined_image_sampler_count > 0 ? info.combined_image_sampler_count : 1;

    VkDescriptorPoolSize pool_sizes[] = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, sampler_count },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 },
    };

    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.poolSizeCount = 2;
    pool_info.pPoolSizes = pool_sizes;
    pool_info.maxSets = info.max_sets > 0 ? info.max_sets : 2;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

    if (vkCreateDescriptorPool(info.device, &pool_info, nullptr, &pool_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create descriptor pool");

    VkDescriptorSetAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = pool_;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts = &layout_;

    if (vkAllocateDescriptorSets(info.device, &alloc_info, &set_) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate descriptor sets");

    VkDescriptorBufferInfo buffer_info{};
    buffer_info.buffer = info.ubo_buffer;
    buffer_info.offset = 0;
    buffer_info.range = info.ubo_range;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set_;
    write.dstBinding = 0;
    write.dstArrayElement = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.descriptorCount = 1;
    write.pBufferInfo = &buffer_info;

    vkUpdateDescriptorSets(info.device, 1, &write, 0, nullptr);
}

void FrameDescriptors::destroy(VkDevice device)
{
    if (!device)
        return;

    // Pool free destroys allocated sets; clear handle first.
    set_ = VK_NULL_HANDLE;

    if (pool_ != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(device, pool_, nullptr);
        pool_ = VK_NULL_HANDLE;
    }
    if (layout_ != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(device, layout_, nullptr);
        layout_ = VK_NULL_HANDLE;
    }
}
