#include "graphics/pch.h"
#include "graphics/frame/frame_descriptors.hpp"
#include "gpu_prv_lib.h"

#include <stdexcept>

void FrameDescriptors::create(const FrameDescriptorsCreateInfo& info)
{
    if (!info.device || !info.ubo_buffer || info.ubo_range == 0)
        throw std::runtime_error("FrameDescriptors::create: invalid args");

    destroy(info.device);

    const DescriptorBinding ubo_bind{
        0,
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        1,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    layout_ = create_descriptor_set_layout(info.device, &ubo_bind, 1);

    const uint32_t sampler_count =
        info.combined_image_sampler_count > 0 ? info.combined_image_sampler_count : 1;
    const VkDescriptorPoolSize pool_sizes[] = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, sampler_count },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 },
    };
    const uint32_t max_sets = info.max_sets > 0 ? info.max_sets : 2;
    pool_ = create_descriptor_pool(info.device, max_sets, pool_sizes, 2);

    if (!allocate_descriptor_sets(info.device, pool_, &layout_, 1, &set_))
        throw std::runtime_error("FrameDescriptors: allocate set failed");

    const DescriptorBufferWrite bw{
        set_,
        0,
        info.ubo_buffer,
        0,
        info.ubo_range,
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
    };
    write_descriptor_buffers(info.device, &bw, 1);
}

void FrameDescriptors::destroy(VkDevice device)
{
    if (!device)
        return;

    // Pool free destroys allocated sets; clear handle first.
    set_ = VK_NULL_HANDLE;

    if (pool_ != VK_NULL_HANDLE)
    {
        destroy_descriptor_pool(device, pool_);
        pool_ = VK_NULL_HANDLE;
    }
    if (layout_ != VK_NULL_HANDLE)
    {
        destroy_descriptor_set_layout(device, layout_);
        layout_ = VK_NULL_HANDLE;
    }
}
