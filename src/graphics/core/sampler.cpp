#include "graphics/pch.h"
#include "graphics/core/sampler.hpp"

#include <stdexcept>

VkSampler create_sampler(VkDevice device, const VkSamplerCreateInfo& info)
{
    if (device == VK_NULL_HANDLE)
        throw std::runtime_error("create_sampler: null device");
    VkSampler sampler = VK_NULL_HANDLE;
    if (vkCreateSampler(device, &info, nullptr, &sampler) != VK_SUCCESS)
        throw std::runtime_error("create_sampler failed");
    return sampler;
}

void destroy_sampler(VkDevice device, VkSampler sampler)
{
    if (device != VK_NULL_HANDLE && sampler != VK_NULL_HANDLE)
        vkDestroySampler(device, sampler, nullptr);
}

void SamplerTable::create(VkDevice device)
{
    destroy(device);
    if (device == VK_NULL_HANDLE)
        throw std::runtime_error("SamplerTable::create: null device");
    device_ = device;

    auto make = [&](VkFilter filter, VkSamplerAddressMode address) -> VkSampler {
        VkSamplerCreateInfo ci{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        ci.magFilter = filter;
        ci.minFilter = filter;
        ci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        ci.addressModeU = address;
        ci.addressModeV = address;
        ci.addressModeW = address;
        ci.minLod = 0.f;
        ci.maxLod = 0.f;
        return create_sampler(device, ci);
    };

    samplers_[static_cast<size_t>(SamplerFilter::NearestClamp)] =
        make(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    samplers_[static_cast<size_t>(SamplerFilter::LinearClamp)] =
        make(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
}

void SamplerTable::destroy(VkDevice device)
{
    const VkDevice dev = device != VK_NULL_HANDLE ? device : device_;
    for (size_t i = 0; i < static_cast<size_t>(SamplerFilter::Count); ++i)
    {
        destroy_sampler(dev, samplers_[i]);
        samplers_[i] = VK_NULL_HANDLE;
    }
    device_ = VK_NULL_HANDLE;
}

VkSampler SamplerTable::get(SamplerFilter f) const
{
    const size_t i = static_cast<size_t>(f);
    if (i >= static_cast<size_t>(SamplerFilter::Count))
        return VK_NULL_HANDLE;
    return samplers_[i];
}
