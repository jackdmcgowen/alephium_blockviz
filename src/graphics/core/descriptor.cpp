#include "graphics/pch.h"
#include "gpu_prv_lib.h"

#include <stdexcept>
#include <vector>

VkDescriptorSetLayout create_descriptor_set_layout(VkDevice device,
                                                   const DescriptorBinding* bindings,
                                                   uint32_t binding_count)
{
    if (device == VK_NULL_HANDLE || !bindings || binding_count == 0)
        throw std::runtime_error("create_descriptor_set_layout: invalid args");

    std::vector<VkDescriptorSetLayoutBinding> native(binding_count);
    for (uint32_t i = 0; i < binding_count; ++i)
    {
        native[i] = {};
        native[i].binding = bindings[i].binding;
        native[i].descriptorType = bindings[i].type;
        native[i].descriptorCount = bindings[i].count > 0 ? bindings[i].count : 1u;
        native[i].stageFlags = bindings[i].stages;
        native[i].pImmutableSamplers = nullptr;
    }

    VkDescriptorSetLayoutCreateInfo info{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    info.bindingCount = binding_count;
    info.pBindings = native.data();

    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    if (vkCreateDescriptorSetLayout(device, &info, nullptr, &layout) != VK_SUCCESS)
        throw std::runtime_error("create_descriptor_set_layout failed");
    return layout;
}

void destroy_descriptor_set_layout(VkDevice device, VkDescriptorSetLayout layout)
{
    if (device != VK_NULL_HANDLE && layout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(device, layout, nullptr);
}

VkDescriptorPool create_descriptor_pool(VkDevice device, uint32_t max_sets,
                                        const VkDescriptorPoolSize* sizes,
                                        uint32_t size_count,
                                        VkDescriptorPoolCreateFlags flags)
{
    if (device == VK_NULL_HANDLE || max_sets == 0 || !sizes || size_count == 0)
        throw std::runtime_error("create_descriptor_pool: invalid args");

    VkDescriptorPoolCreateInfo info{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    info.flags = flags;
    info.maxSets = max_sets;
    info.poolSizeCount = size_count;
    info.pPoolSizes = sizes;

    VkDescriptorPool pool = VK_NULL_HANDLE;
    if (vkCreateDescriptorPool(device, &info, nullptr, &pool) != VK_SUCCESS)
        throw std::runtime_error("create_descriptor_pool failed");
    return pool;
}

void destroy_descriptor_pool(VkDevice device, VkDescriptorPool pool)
{
    if (device != VK_NULL_HANDLE && pool != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(device, pool, nullptr);
}

bool allocate_descriptor_sets(VkDevice device, VkDescriptorPool pool,
                              const VkDescriptorSetLayout* layouts, uint32_t count,
                              VkDescriptorSet* out_sets)
{
    if (device == VK_NULL_HANDLE || pool == VK_NULL_HANDLE || !layouts || count == 0 ||
        !out_sets)
        return false;

    VkDescriptorSetAllocateInfo info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    info.descriptorPool = pool;
    info.descriptorSetCount = count;
    info.pSetLayouts = layouts;
    return vkAllocateDescriptorSets(device, &info, out_sets) == VK_SUCCESS;
}

void write_descriptor_buffers(VkDevice device, const DescriptorBufferWrite* writes,
                              uint32_t count)
{
    if (device == VK_NULL_HANDLE || !writes || count == 0)
        return;

    std::vector<VkDescriptorBufferInfo> bufs(count);
    std::vector<VkWriteDescriptorSet> ws(count);
    for (uint32_t i = 0; i < count; ++i)
    {
        bufs[i] = {};
        bufs[i].buffer = writes[i].buffer;
        bufs[i].offset = writes[i].offset;
        bufs[i].range = writes[i].range;

        ws[i] = {};
        ws[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        ws[i].dstSet = writes[i].set;
        ws[i].dstBinding = writes[i].binding;
        ws[i].dstArrayElement = 0;
        ws[i].descriptorType = writes[i].type;
        ws[i].descriptorCount = 1;
        ws[i].pBufferInfo = &bufs[i];
    }
    vkUpdateDescriptorSets(device, count, ws.data(), 0, nullptr);
}

void write_descriptor_images(VkDevice device, const DescriptorImageWrite* writes,
                             uint32_t count)
{
    if (device == VK_NULL_HANDLE || !writes || count == 0)
        return;

    std::vector<VkDescriptorImageInfo> imgs(count);
    std::vector<VkWriteDescriptorSet> ws(count);
    for (uint32_t i = 0; i < count; ++i)
    {
        imgs[i] = {};
        imgs[i].sampler = writes[i].sampler;
        imgs[i].imageView = writes[i].image_view;
        imgs[i].imageLayout = writes[i].image_layout;

        ws[i] = {};
        ws[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        ws[i].dstSet = writes[i].set;
        ws[i].dstBinding = writes[i].binding;
        ws[i].dstArrayElement = 0;
        ws[i].descriptorType = writes[i].type;
        ws[i].descriptorCount = 1;
        ws[i].pImageInfo = &imgs[i];
    }
    vkUpdateDescriptorSets(device, count, ws.data(), 0, nullptr);
}
