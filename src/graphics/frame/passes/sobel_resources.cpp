#include "graphics/pch.h"
#include "graphics/frame/passes/sobel_resources.hpp"
#include "graphics/gpu_prv_lib.h"

#include <stdexcept>

namespace frame_graph
{
namespace
{
static constexpr VkFormat kOutlineColorFormat = VK_FORMAT_R8G8B8A8_UNORM;
} // namespace

void SobelResources::create_images_(const PassCreateInfo& info)
{
    create_image(
        info.device, info.width, info.height, info.depth_format,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        sel_depth_image_, sel_depth_memory_, info.mem_props);
    sel_depth_view_ = create_image_view(info.device, sel_depth_image_, info.depth_format,
                                        VK_IMAGE_ASPECT_DEPTH_BIT);

    create_image(
        info.device, info.width, info.height, kOutlineColorFormat,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        outline_color_image_, outline_color_memory_, info.mem_props);
    outline_color_view_ = create_image_view(info.device, outline_color_image_,
                                            kOutlineColorFormat, VK_IMAGE_ASPECT_COLOR_BIT);

    create_image(
        info.device, info.width, info.height, VK_FORMAT_R8_UNORM,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        edge_image_, edge_memory_, info.mem_props);
    edge_view_ = create_image_view(info.device, edge_image_, VK_FORMAT_R8_UNORM,
                                   VK_IMAGE_ASPECT_COLOR_BIT);
}

void SobelResources::create_descriptors_(VkDevice device)
{
    if (!samplers_ || !samplers_->ready())
        throw std::runtime_error("SobelResources: SamplerTable required");
    const VkSampler nearest = samplers_->get(SamplerFilter::NearestClamp);
    if (nearest == VK_NULL_HANDLE)
        throw std::runtime_error("SobelResources: NearestClamp sampler missing");

    const DescriptorBinding compute_binds[] = {
        { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT },
        { 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT },
    };
    compute_set_layout_ = create_descriptor_set_layout(device, compute_binds, 2);

    const DescriptorBinding overlay_binds[] = {
        { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT },
        { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT },
    };
    overlay_set_layout_ = create_descriptor_set_layout(device, overlay_binds, 2);

    const VkDescriptorPoolSize sizes[] = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 6 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2 },
    };
    descriptor_pool_ = create_descriptor_pool(device, 4, sizes, 2);

    if (!allocate_descriptor_sets(device, descriptor_pool_, &compute_set_layout_, 1,
                                  &compute_set_) ||
        !allocate_descriptor_sets(device, descriptor_pool_, &overlay_set_layout_, 1,
                                  &overlay_set_))
        throw std::runtime_error("SobelResources: allocate descriptor sets");

    write_static_descriptors_();
}

void SobelResources::write_static_descriptors_()
{
    const VkSampler nearest =
        samplers_ ? samplers_->get(SamplerFilter::NearestClamp) : VK_NULL_HANDLE;
    const DescriptorImageWrite compute_imgs[] = {
        { compute_set_, 0, sel_depth_view_, nearest,
          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
          VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER },
        { compute_set_, 1, edge_view_, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL,
          VK_DESCRIPTOR_TYPE_STORAGE_IMAGE },
    };
    write_descriptor_images(device_, compute_imgs, 2);

    const DescriptorImageWrite overlay_imgs[] = {
        { overlay_set_, 0, edge_view_, nearest,
          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
          VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER },
        { overlay_set_, 1, outline_color_view_, nearest,
          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
          VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER },
    };
    write_descriptor_images(device_, overlay_imgs, 2);
}

void SobelResources::create_sync_(VkDevice device)
{
    VkSemaphoreCreateInfo sci{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    for (uint32_t i = 0; i < kSobelMaxFrames; ++i)
    {
        if (vkCreateSemaphore(device, &sci, nullptr, &compute_finished_[i]) != VK_SUCCESS ||
            vkCreateSemaphore(device, &sci, nullptr, &graphics_to_compute_[i]) != VK_SUCCESS)
            throw std::runtime_error("SobelResources: per-frame semaphores");
    }
}

VkSemaphore SobelResources::graphics_to_compute(uint32_t frame_index) const
{
    return graphics_to_compute_[frame_index % kSobelMaxFrames];
}

VkSemaphore SobelResources::compute_finished(uint32_t frame_index) const
{
    return compute_finished_[frame_index % kSobelMaxFrames];
}

void SobelResources::create(const PassCreateInfo& info)
{
    if (!info.device || !info.mem_props || info.width == 0 || info.height == 0)
        throw std::runtime_error("SobelResources::create: invalid args");
    if (!info.samplers || !info.samplers->ready())
        throw std::runtime_error("SobelResources::create: SamplerTable required");
    if (info.depth_format == VK_FORMAT_UNDEFINED)
        throw std::runtime_error("SobelResources::create: depth_format required");

    device_ = info.device;
    width_ = info.width;
    height_ = info.height;
    graphics_family_ = info.graphics_family;
    compute_family_ = info.compute_family;
    depth_format_ = info.depth_format;
    samplers_ = info.samplers;

    create_images_(info);
    create_descriptors_(info.device);
    create_sync_(info.device);
}

void SobelResources::destroy(VkDevice device)
{
    if (!device)
        return;

    for (uint32_t i = 0; i < kSobelMaxFrames; ++i)
    {
        if (graphics_to_compute_[i] != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(device, graphics_to_compute_[i], nullptr);
            graphics_to_compute_[i] = VK_NULL_HANDLE;
        }
        if (compute_finished_[i] != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(device, compute_finished_[i], nullptr);
            compute_finished_[i] = VK_NULL_HANDLE;
        }
    }

    if (descriptor_pool_)
    {
        destroy_descriptor_pool(device, descriptor_pool_);
        descriptor_pool_ = VK_NULL_HANDLE;
        compute_set_ = overlay_set_ = VK_NULL_HANDLE;
    }
    if (compute_set_layout_)
    {
        destroy_descriptor_set_layout(device, compute_set_layout_);
        compute_set_layout_ = VK_NULL_HANDLE;
    }
    if (overlay_set_layout_)
    {
        destroy_descriptor_set_layout(device, overlay_set_layout_);
        overlay_set_layout_ = VK_NULL_HANDLE;
    }
    samplers_ = nullptr;

    if (edge_view_)
    {
        destroy_image_view(device, edge_view_);
        edge_view_ = VK_NULL_HANDLE;
    }
    if (edge_image_)
    {
        destroy_image(device, edge_image_, edge_memory_);
        edge_image_ = VK_NULL_HANDLE;
        edge_memory_ = VK_NULL_HANDLE;
    }
    if (outline_color_view_)
    {
        destroy_image_view(device, outline_color_view_);
        outline_color_view_ = VK_NULL_HANDLE;
    }
    if (outline_color_image_)
    {
        destroy_image(device, outline_color_image_, outline_color_memory_);
        outline_color_image_ = VK_NULL_HANDLE;
        outline_color_memory_ = VK_NULL_HANDLE;
    }
    if (sel_depth_view_)
    {
        destroy_image_view(device, sel_depth_view_);
        sel_depth_view_ = VK_NULL_HANDLE;
    }
    if (sel_depth_image_)
    {
        destroy_image(device, sel_depth_image_, sel_depth_memory_);
        sel_depth_image_ = VK_NULL_HANDLE;
        sel_depth_memory_ = VK_NULL_HANDLE;
    }

    device_ = VK_NULL_HANDLE;
    width_ = height_ = 0;
}

void SobelResources::recreate(const PassCreateInfo& info)
{
    destroy(info.device);
    create(info);
}

} // namespace frame_graph
